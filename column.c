#include "cache.h"
#include "column.h"
#include "string-list.h"
#include "parse-options.h"
#include "run-command.h"
#include "utf8.h"

#define XY2LINEAR(d, x, y) (COL_LAYOUT((d)->colopts) == COL_COLUMN ? \
			    (x) * (d)->rows + (y) : \
			    (y) * (d)->cols + (x))

struct column_data {
	const struct string_list *list;
	unsigned int colopts;
	struct column_options opts;

	int rows, cols;
	int *len;		/* cell length */
	int *width;	      /* index to the longest row in column */
};

struct area {
	int start, end;		/* in string_list */
	int size;
};

/* return length of 's' in letters, ANSI escapes stripped */
static int item_length(unsigned int colopts, const char *s)
{
	int len, i = 0;
	struct strbuf str = STRBUF_INIT;

	strbuf_addstr(&str, s);
	while ((s = strstr(str.buf + i, "\033[")) != NULL) {
		int len = strspn(s + 2, "0123456789;");
		i = s - str.buf;
		strbuf_remove(&str, i, len + 3); /* \033[<len><func char> */
	}
	len = utf8_strwidth(str.buf);
	strbuf_release(&str);
	return len;
}

/*
 * Calculate cell width, rows and cols for a table of equal cells, given
 * table width and how many spaces between cells.
 */
static void layout(struct column_data *data, int *width)
{
	int i;

	*width = 0;
	for (i = 0; i < data->list->nr; i++)
		if (*width < data->len[i])
			*width = data->len[i];

	*width += data->opts.padding;

	data->cols = (data->opts.width - strlen(data->opts.indent)) / *width;
	if (data->cols == 0)
		data->cols = 1;

	data->rows = DIV_ROUND_UP(data->list->nr, data->cols);
}

static void compute_column_width(struct column_data *data)
{
	int i, x, y;
	for (x = 0; x < data->cols; x++) {
		data->width[x] = XY2LINEAR(data, x, 0);
		for (y = 0; y < data->rows; y++) {
			i = XY2LINEAR(data, x, y);
			if (i < data->list->nr &&
			    data->len[data->width[x]] < data->len[i])
				data->width[x] = i;
		}
	}
}

/*
 * Shrink all columns by shortening them one row each time (and adding
 * more columns along the way). Hopefully the longest cell will be
 * moved to the next column, column is shrunk so we have more space
 * for new columns. The process ends when the whole thing no longer
 * fits in data->total_width.
 */
static void shrink_columns(struct column_data *data)
{
	data->width = xrealloc(data->width,
			       sizeof(*data->width) * data->cols);
	while (data->rows > 1) {
		int x, total_width, cols, rows;
		rows = data->rows;
		cols = data->cols;

		data->rows--;
		data->cols = DIV_ROUND_UP(data->list->nr, data->rows);
		if (data->cols != cols)
			data->width = xrealloc(data->width,
					       sizeof(*data->width) * data->cols);
		compute_column_width(data);

		total_width = strlen(data->opts.indent);
		for (x = 0; x < data->cols; x++) {
			total_width += data->len[data->width[x]];
			total_width += data->opts.padding;
		}
		if (total_width > data->opts.width) {
			data->rows = rows;
			data->cols = cols;
			break;
		}
	}
	compute_column_width(data);
}

/* Display without layout when not enabled */
static void display_plain(const struct string_list *list,
			  const char *indent, const char *nl)
{
	int i;

	for (i = 0; i < list->nr; i++)
		printf("%s%s%s", indent, list->items[i].string, nl);
}

/* Print a cell to stdout with all necessary leading/traling space */
static int display_cell(struct column_data *data, int initial_width,
			const char *empty_cell, int x, int y)
{
	int i, len, newline;

	i = XY2LINEAR(data, x, y);
	if (i >= data->list->nr)
		return -1;

	len = data->len[i];
	if (data->width && data->len[data->width[x]] < initial_width) {
		/*
		 * empty_cell has initial_width chars, if real column
		 * is narrower, increase len a bit so we fill less
		 * space.
		 */
		len += initial_width - data->len[data->width[x]];
		len -= data->opts.padding;
	}

	if (COL_LAYOUT(data->colopts) == COL_COLUMN)
		newline = i + data->rows >= data->list->nr;
	else
		newline = x == data->cols - 1 || i == data->list->nr - 1;

	printf("%s%s%s",
	       x == 0 ? data->opts.indent : "",
	       data->list->items[i].string,
	       newline ? data->opts.nl : empty_cell + len);
	return 0;
}

/*
 * Attempt to put the longest cell into a separate line, see if it
 * improves the layout
 */
static int break_long_line(const struct column_data *old_data)
{
	struct column_data data;
	struct string_list faked_list;
	int initial_width, x, y, i, item = 0, row1, row2;
	char *empty_cell;

	memcpy(&data, old_data, sizeof(data));
	for (i = 0; i < data.list->nr; i++)
		if (data.len[i] > data.len[item])
			item = i;
	data.list = &faked_list;
	data.width = NULL;
	faked_list = *old_data->list;

	faked_list.nr = item + 1;
	layout(&data, &initial_width);
	shrink_columns(&data);
	row1 = data.rows;

	faked_list.nr = item;
	layout(&data, &initial_width);
	shrink_columns(&data);
	row2 = data.rows;

	if (row1 - row2 < 3)
		return -1;

	empty_cell = xmalloc(initial_width + 1);
	memset(empty_cell, ' ', initial_width);
	empty_cell[initial_width] = '\0';
	for (y = 0; y < data.rows; y++) {
		for (x = 0; x < data.cols; x++)
			if (display_cell(&data, initial_width, empty_cell, x, y))
				break;
	}
	free(data.width);
	free(empty_cell);
	return item;
}

/* Display COL_COLUMN or COL_ROW */
static int display_table(const struct string_list *list,
			 unsigned int colopts,
			 const struct column_options *opts)
{
	struct column_data data;
	int x, y, i, initial_width;
	char *empty_cell;

	memset(&data, 0, sizeof(data));
	data.list = list;
	data.colopts = colopts;
	data.opts = *opts;

	data.len = xmalloc(sizeof(*data.len) * list->nr);
	for (i = 0; i < list->nr; i++)
		data.len[i] = item_length(colopts, list->items[i].string);

	layout(&data, &initial_width);

	if (colopts & COL_DENSE)
		shrink_columns(&data);
	if (colopts & COL_DENSER) {
		i = break_long_line(&data);
		if (i != -1) {
			printf("%s%s" "%s%s%s" "%s%s",
			       opts->indent, opts->nl,
			       opts->indent, list->items[i].string, opts->nl,
			       opts->indent, opts->nl);
			free(data.len);
			free(data.width);
			return i + 1;
		}
		shrink_columns(&data);
	}

	empty_cell = xmalloc(initial_width + 1);
	memset(empty_cell, ' ', initial_width);
	empty_cell[initial_width] = '\0';
	for (y = 0; y < data.rows; y++) {
		for (x = 0; x < data.cols; x++)
			if (display_cell(&data, initial_width, empty_cell, x, y))
				break;
	}

	free(data.len);
	free(data.width);
	free(empty_cell);
	return list->nr;
}

/*
 * Find out the contiguous list of entries sharing the same directory
 * prefix that nr * (prefix_len - skip) is largest, where nr is the
 * number of entries and prefix_len is the shared directory prefix's
 * length.
 */
static int largest_block(const struct string_list *list, int start, int skip, int *len)
{
	const char *str = list->items[start].string;
	const char *slash;
	int largest_area = 0;

	for (slash = str + strlen(str) - 1; slash > str + skip; slash--) {
		int i, area;
		if (*slash != '/')
			continue;
		for (i = start; i < list->nr; i++) {
			const char *s = list->items[i].string;
			if (strlen(s) < slash + 1 - str ||
			    memcmp(str + skip, s + skip, slash + 1 - (str + skip)))
				break;
		}
		area = (i - start) * (slash + 1 - str - skip);
		if (area > largest_area) {
			largest_area = area;
			*len = i - start;
		}
	}
	return largest_area;
}

static int area_size_cmp(const void *a, const void *b)
{
	const struct area *area1 = a;
	const struct area *area2 = b;
	return area2->size - area1->size;
}

/*
 * Make a sorted list of non-overlapping blocks, largest ones first
 */
static struct area *find_large_blocks(const struct string_list *list, int *nr_p)
{
	int i, nr = 0, alloc = 16;
	struct area *areas = xmalloc(sizeof(*areas) * alloc);
	struct area last;
	memset(&last, 0, sizeof(last));

	for (i = 0; i < list->nr; i++) {
		int len, size = largest_block(list, i, 0, &len);
		if (!size || len == 1)
			continue;
		/* the new found area is overlapped with the old one,
		   but smaller, skip it */
		if (i < last.end) {
			if (size < last.size)
				continue;
			last.start = i;
			last.end = i + len;
			last.size = size;
			continue;
		}
		if (last.size) {
			if (nr + 1 < alloc)
				ALLOC_GROW(areas, nr + 1, alloc);
			areas[nr++] = last;
		}
		last.start = i;
		last.end = i + len;
		last.size = size;
	}
	if (last.size) {
		if (nr + 1 >= alloc)
			ALLOC_GROW(areas, nr + 1, alloc);
		areas[nr++] = last;
	}
	qsort(areas, nr, sizeof(*areas), area_size_cmp);
	*nr_p = nr;
	return areas;
}

static int area_start_cmp(const void *a, const void *b)
{
	const struct area *area1 = a;
	const struct area *area2 = b;
	return area1->start - area2->start;
}

/*
 * Assume list is split into two tables: one from "start" to "stop",
 * where all strings are truncated "skip" bytes, the other the rest of
 * the strings. Calculate how many rows required if all cells of each
 * table are of the same width.
 */
static int split_layout_gain(const struct string_list *list, int *lengths,
			     const struct column_options *opts,
			     int start, int stop, int skip)
{
	int i, width0, width1, width2, cols, rows0, rows1;
	int indent = strlen(opts->indent);

	width0 = width1 = width2 = 0;
	for (i = 0; i < list->nr; i++) {
		int len = lengths[i];
		if (width0 < len)
			width0 = len;
		if (i >= start && i < stop) {
			len -= skip;
			if (width2 < len)
				width2 = len;
		} else {
			if (width1 < len)
				width1 = len;
		}
	}

	width0 += opts->padding;
	cols = (opts->width - indent) / width0;
	if (cols == 0)
		cols = 1;
	rows0 = DIV_ROUND_UP(list->nr, cols);

	width1 += opts->padding;
	cols = (opts->width - indent) / width1;
	if (cols == 0)
		cols = 1;
	rows1 = DIV_ROUND_UP(list->nr - (stop - start), cols);

	width2 += opts->padding;
	cols = (opts->width - indent) / width2;
	if (cols == 0)
		cols = 1;
	rows1 += DIV_ROUND_UP(stop - start, cols);
	return rows0 - rows1;
}

static void group_by_prefix(const struct string_list *list, unsigned int colopts,
			    const struct column_options *opts)
{
	int i, nr;
	struct area *areas = find_large_blocks(list, &nr);
	struct string_list new_list = STRING_LIST_INIT_NODUP;
	struct area *dst;
	int *len;

	assert(colopts & COL_GROUP);
	/* avoid inifinite loop when calling print_columns again */
	colopts &= ~COL_GROUP;

	len = xmalloc(sizeof(*len) * list->nr);
	for (i = 0; i < list->nr; i++)
		len[i] = item_length(colopts, list->items[i].string);

	/*
	 * Calculate and see if there is any saving when print this as
	 * a group. Base our calculation on non-dense mode even if
	 * users want dense output because the calculation would be
	 * less expensive.
	 */
	dst = areas;
	for (i = 0; i < nr; i++) {
		struct area *area = areas + i;
		int rows, skip = area->size / (area->end - area->start);
		rows = split_layout_gain(list, len, opts,
					 area->start, area->end, skip);

		if (rows > 3) {
			if (areas + i != dst)
				*dst = *area;
			dst++;
		}
	}
	free(len);

	nr = dst - areas;
	if (!nr) {
		print_columns(list, colopts, opts);
		return;
	}
	qsort(areas, nr, sizeof(*areas), area_start_cmp);

	/*
	 * We now have list of worthy groups, sorted by offset. Print
	 * group by group, then the rest.
	 */
	for (i = 0; i < nr; i++) {
		struct area *area = areas + i;
		int j, skip = area->size / (area->end - area->start);

		for (j = area->start; j < area->end; j++)
			string_list_append(&new_list,
					   list->items[j].string + skip);
		printf("\n%.*s:\n", skip, list->items[area->start].string);
		print_columns(&new_list, colopts, opts);
		string_list_clear(&new_list, 0);
	}

	printf("\n%s:\n", "...");
	for (i = 0; i < nr; i++) {
		struct area *area = areas + i;
		int j;
		for (j = i ? area[-1].end : 0; j < area->start; j++)
			string_list_append(&new_list, list->items[j].string);
	}
	for (i = areas[nr-1].end; i < list->nr; i++)
		string_list_append(&new_list, list->items[i].string);
	print_columns(&new_list, colopts, opts);
	string_list_clear(&new_list, 0);

	free(areas);
}

void print_columns(const struct string_list *list, unsigned int colopts,
		   const struct column_options *opts)
{
	struct column_options nopts;
	int processed;
	struct string_list l = *list;

	if (!list->nr)
		return;
	assert(COL_ENABLE(colopts) != COL_AUTO);

	memset(&nopts, 0, sizeof(nopts));
	nopts.indent = opts && opts->indent ? opts->indent : "";
	nopts.nl = opts && opts->nl ? opts->nl : "\n";
	nopts.padding = opts ? opts->padding : 1;
	nopts.width = opts && opts->width ? opts->width : term_columns() - 1;

	if (colopts & COL_GROUP) {
		group_by_prefix(list, colopts, &nopts);
		return;
	}
	if (!COL_ENABLE(colopts)) {
		display_plain(list, "", "\n");
		return;
	}
	switch (COL_LAYOUT(colopts)) {
	case COL_PLAIN:
		display_plain(list, nopts.indent, nopts.nl);
		break;
	case COL_ROW:
	case COL_COLUMN:
		while (l.nr &&
		       (processed = display_table(&l, colopts, &nopts)) < l.nr) {
			l.items += processed;
			l.nr -= processed;
		}
		break;
	default:
		die("BUG: invalid layout mode %d", COL_LAYOUT(colopts));
	}
}

int finalize_colopts(unsigned int *colopts, int stdout_is_tty)
{
	if (COL_ENABLE(*colopts) == COL_AUTO) {
		if (stdout_is_tty < 0)
			stdout_is_tty = isatty(1);
		*colopts &= ~COL_ENABLE_MASK;
		if (stdout_is_tty)
			*colopts |= COL_ENABLED;
	}
	return 0;
}

struct colopt {
	const char *name;
	unsigned int value;
	unsigned int mask;
};

#define LAYOUT_SET 1
#define ENABLE_SET 2

static int parse_option(const char *arg, int len, unsigned int *colopts,
			int *group_set)
{
	struct colopt opts[] = {
		{ "always", COL_ENABLED,  COL_ENABLE_MASK },
		{ "never",  COL_DISABLED, COL_ENABLE_MASK },
		{ "auto",   COL_AUTO,     COL_ENABLE_MASK },
		{ "plain",  COL_PLAIN,    COL_LAYOUT_MASK },
		{ "column", COL_COLUMN,   COL_LAYOUT_MASK },
		{ "row",    COL_ROW,      COL_LAYOUT_MASK },
		{ "dense",  COL_DENSE,    0 },
		{ "denser", COL_DENSER,   0 },
		{ "group",  COL_GROUP,    0 },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(opts); i++) {
		int set = 1, arg_len = len, name_len;
		const char *arg_str = arg;

		if (!opts[i].mask) {
			if (arg_len > 2 && !strncmp(arg_str, "no", 2)) {
				arg_str += 2;
				arg_len -= 2;
				set = 0;
			}
		}

		name_len = strlen(opts[i].name);
		if (arg_len != name_len ||
		    strncmp(arg_str, opts[i].name, name_len))
			continue;

		switch (opts[i].mask) {
		case COL_ENABLE_MASK:
			*group_set |= ENABLE_SET;
			break;
		case COL_LAYOUT_MASK:
			*group_set |= LAYOUT_SET;
			break;
		}

		if (opts[i].mask)
			*colopts = (*colopts & ~opts[i].mask) | opts[i].value;
		else {
			if (set)
				*colopts |= opts[i].value;
			else
				*colopts &= ~opts[i].value;
		}
		return 0;
	}

	return error("unsupported option '%s'", arg);
}

static int parse_config(unsigned int *colopts, const char *value)
{
	const char *sep = " ,";
	int group_set = 0;

	while (*value) {
		int len = strcspn(value, sep);
		if (len) {
			if (parse_option(value, len, colopts, &group_set))
				return -1;

			value += len;
		}
		value += strspn(value, sep);
	}
	/*
	 * Setting layout implies "always" if neither always, never
	 * nor auto is specified.
	 *
	 * Current COL_ENABLE() value is disregarded. This means if
	 * you set column.ui = auto and pass --column=row, then "auto"
	 * will become "always".
	 */
	if ((group_set & LAYOUT_SET) && !(group_set & ENABLE_SET))
		*colopts = (*colopts & ~COL_ENABLE_MASK) | COL_ENABLED;
	return 0;
}

static int column_config(const char *var, const char *value,
			 const char *key, unsigned int *colopts)
{
	if (parse_config(colopts, value))
		return error("invalid %s mode %s", key, value);
	return 0;
}

int git_column_config(const char *var, const char *value,
		      const char *command, unsigned int *colopts)
{
	if (!strcmp(var, "column.ui"))
		return column_config(var, value, "column.ui", colopts);

	if (command) {
		struct strbuf sb = STRBUF_INIT;
		int ret = 0;
		strbuf_addf(&sb, "column.%s", command);
		if (!strcmp(var, sb.buf))
			ret = column_config(var, value, sb.buf, colopts);
		strbuf_release(&sb);
		return ret;
	}

	return 0;
}

int parseopt_column_callback(const struct option *opt,
			     const char *arg, int unset)
{
	unsigned int *colopts = opt->value;
	*colopts |= COL_PARSEOPT;
	*colopts &= ~COL_ENABLE_MASK;
	if (unset)		/* --no-column == never */
		return 0;
	/* --column == always unless "arg" states otherwise */
	*colopts |= COL_ENABLED;
	if (arg)
		return parse_config(colopts, arg);

	return 0;
}

static int fd_out = -1;
static struct child_process column_process;

int run_column_filter(int colopts, const struct column_options *opts)
{
	const char *av[10];
	int ret, ac = 0;
	struct strbuf sb_colopt  = STRBUF_INIT;
	struct strbuf sb_width   = STRBUF_INIT;
	struct strbuf sb_padding = STRBUF_INIT;

	if (fd_out != -1)
		return -1;

	av[ac++] = "column";
	strbuf_addf(&sb_colopt, "--raw-mode=%d", colopts);
	av[ac++] = sb_colopt.buf;
	if (opts && opts->width) {
		strbuf_addf(&sb_width, "--width=%d", opts->width);
		av[ac++] = sb_width.buf;
	}
	if (opts && opts->indent) {
		av[ac++] = "--indent";
		av[ac++] = opts->indent;
	}
	if (opts && opts->padding) {
		strbuf_addf(&sb_padding, "--padding=%d", opts->padding);
		av[ac++] = sb_padding.buf;
	}
	av[ac] = NULL;

	fflush(stdout);
	memset(&column_process, 0, sizeof(column_process));
	column_process.in = -1;
	column_process.out = dup(1);
	column_process.git_cmd = 1;
	column_process.argv = av;

	ret = start_command(&column_process);

	strbuf_release(&sb_colopt);
	strbuf_release(&sb_width);
	strbuf_release(&sb_padding);

	if (ret)
		return -2;

	fd_out = dup(1);
	close(1);
	dup2(column_process.in, 1);
	close(column_process.in);
	return 0;
}

int stop_column_filter(void)
{
	if (fd_out == -1)
		return -1;

	fflush(stdout);
	close(1);
	finish_command(&column_process);
	dup2(fd_out, 1);
	close(fd_out);
	fd_out = -1;
	return 0;
}
