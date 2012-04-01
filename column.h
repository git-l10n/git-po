#ifndef COLUMN_H
#define COLUMN_H

#define COL_LAYOUT_MASK   0x000F
#define COL_ENABLE_MASK   0x0030   /* always, never or auto */
#define COL_PARSEOPT      0x0040   /* --column is given from cmdline */
#define COL_DENSE         0x0080   /* Shrink columns when possible,
				      making space for more columns */
#define COL_DENSER        0x0100
#define COL_GROUP         0x0200

#define COL_ENABLE(c) ((c) & COL_ENABLE_MASK)
#define COL_DISABLED      0x0000   /* must be zero */
#define COL_ENABLED       0x0010
#define COL_AUTO          0x0020

#define COL_LAYOUT(c) ((c) & COL_LAYOUT_MASK)
#define COL_COLUMN             0   /* Fill columns before rows */
#define COL_ROW                1   /* Fill rows before columns */
#define COL_PLAIN             15   /* one column */

#define explicitly_enable_column(c) \
	(((c) & COL_PARSEOPT) && COL_ENABLE(c) == COL_ENABLED)

struct column_options {
	int width;
	int padding;
	const char *indent;
	const char *nl;
};

struct option;
extern int parseopt_column_callback(const struct option *, const char *, int);
extern int git_column_config(const char *var, const char *value,
			     const char *command, unsigned int *colopts);
extern int finalize_colopts(unsigned int *colopts, int stdout_is_tty);

extern void print_columns(const struct string_list *list, unsigned int colopts,
			  const struct column_options *opts);

extern int run_column_filter(int colopts, const struct column_options *);
extern int stop_column_filter(void);

#endif
