# Git L10n Agent Guide

This document summarizes the Git localization capabilities for agents
that operate on `po/XX.po` files. It is based on `po/README.md` and
local conventions in this repository.

## 1) Map target language to `po/XX.po`

Two supported methods:

- **Method A: `po/TEAMS` lookup**
  - Read `po/TEAMS` and match the `Language:` field to find the locale
    code (e.g. `zh_CN`, `pt_PT`, `de`).
  - The `Language:` code directly maps to `po/XX.po` (e.g. `zh_CN` →
    `po/zh_CN.po`).

- **Method B: infer from a `.po` header**
  - Open `po/XX.po` and inspect the header entry (`msgid ""` / `msgstr ""`).
  - Parse the `Language:` field inside the header block to identify the
    locale code, then map it back to the filename (e.g. `Language: zh_CN`
    → `po/zh_CN.po`).
  - If `Language:` is missing, fall back to the human-readable comment
    header (first lines starting with `#`) to infer the language name.

## 2) Create or update the l10n template file `po/git.pot`

From the repository root, run:

```shell
make pot -j10
```

This creates or refreshes `po/git.pot` for downstream updates.

## 3) Update `po/XX.po` based on the template file `po/git.pot`

Before updating `po/XX.po`, detect the location comment style by looking
at the line immediately above each `msgid` entry:

- **Filename + location**: `#: path/file.c:123`
- **Filename only**: `#: path/file.c`
- **No location lines**: no `#:` line present

Choose the proper flags for `git-po-helper update` based on the detected
location comment style:

```shell
git-po-helper update --pot-file=build [--no-file-location|--no-location] XX.po
```

Options based on the detected location comment style:

- **No filename and no location**: `--no-file-location`
- **Filename only (no line numbers)**: `--no-location`
- **Filename + location**: no extra flag

The `--pot-file=build` option uses the locally built `po/git.pot`
instead of downloading a prebuilt `po/git.pot` from GitHub.

## 4) Glossary in header comments

Check the `po/XX.po` header comment block for a glossary section (e.g.
"Git glossary for Chinese translators") and apply those term mappings
to keep terminology consistent across translations. The glossary
section lives in the comments immediately before the first `msgid`
entry.

## 5) Fill new translations and resolve fuzzy entries

For new translations, after generating `po/git.pot` and updating
`po/XX.po`, translate:

- **New entries**: `msgid` is non-empty and `msgstr` is empty.
  - Translate `msgid` into the target language and write to `msgstr`.
- **Fuzzy entries**: lines with `#, fuzzy` in comments.
  - Re-translate based on the current `msgid`.
  - Remove the `fuzzy` tag after updating `msgstr`.
- **Glossary in header comments**: if the header comments contain a
  glossary section (e.g. "Git glossary for Chinese translators"),
  read and apply those term mappings as translation context to keep
  terminology consistent.

Preserve formatting, placeholders, punctuation, and whitespace to match
the English source.

## 6) Split large `po/XX.po` or patch files into chunks

When a `po/XX.po` file or a patch file is too large for model context,
split it into chunks while keeping `msgid` and `msgstr` pairs intact.
This includes plural forms: `msgid`, `msgid_plural`, `msgstr[0]`,
`msgstr[1]`, and additional plural indices required by the language.

For `po/XX.po`, split on the line immediately before each `msgid`
entry. This guarantees that no chunk begins with an unpaired `msgid`.
Use `grep -n '^msgid' po/XX.po` to locate split points, and group the
file into chunks that contain no more than 200 `msgid` entries (about
50K bytes each).

For patch files, first check the patch size:

- If the patch is <= 100KB, do not split.
- If the patch is > 100KB, split it using the same rule as `po/XX.po`:
  split on the line immediately before each `msgid` entry so that
  message pairs stay together.

## 7) Review the entire `po/XX.po` file

When explicitly asked to review all translated content, always review
`po/XX.po` in chunks. Follow the split rules in section 6.

Review translations in the target language by identifying mistranslations,
missing translations, typos, glossary mismatches, and other translation
quality issues. Unless otherwise specified, update the `po/XX.po` file
directly; if a summary is requested, provide a consolidated report of the
issues.

## 8) Review a patch (diff)

Review requests may come as patches of `po/XX.po` for the following cases:

- Workspace changes: `git diff HEAD -- po/XX.po`
- Changes since a commit-ish: `git diff <commit-ish> -- po/XX.po`
- A specific commit: `git show <commit-ish> -- po/XX.po`

If the patch is larger than 100KB, split it into chunks using the rules
in section 6. When diff context is incomplete (truncated `msgid` or
`msgstr`), use file viewing tools to pull nearby context for accurate
review.

Review translations in the target language by identifying mistranslations,
missing translations, typos, glossary mismatches, and other translation
quality issues. Unless otherwise specified, update the `po/XX.po` file
directly; if a summary is requested, provide a consolidated report of the
issues.

## 9) Plural translations

For entries with `msgid_plural`, provide plural forms:

```po
msgid "..."
msgid_plural "..."
msgstr[0] "..."
msgstr[1] "..."
```

Use `msgstr[0]`/`msgstr[1]` as required. If the language has more plural
forms, follow the `Plural-Forms` header in `po/XX.po` to determine the
required number of `msgstr[n]` entries.

## 10) Use positional parameters after reordering arguments

When the translation reorders placeholders, mark them with positional
parameter syntax (`%n$`) so each argument maps to the correct source
value. Keep the width/precision modifiers intact and place the position
before them.

Example:

```po
msgid "missing environment variable '%s' for configuration '%.*s'"
msgstr "配置 '%2$.*3$s' 缺少环境变量 '%1$s'"
```

Here the translation swaps the two placeholders. `%1$s` still refers to
the first argument (`%s`), while `%2$.*3$s` refers to the second string
argument with the precision taken from the third argument (`%.*s`).

## 11) Create commits following existing history

Check prior commit messages for the same file to match style:

```shell
git log --oneline -- po/XX.po
```

Commit conventions from `po/README.md`:

- Subject starts with `l10n: `
- ASCII only, ≤ 50 chars for subject
- Body lines ≤ 72 chars
- Include `Signed-off-by` (use `git commit -s`)
