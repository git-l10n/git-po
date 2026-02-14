# Instructions for AI Agents

This file gives specific instructions for AI agents that perform
housekeeping tasks for Git l10n. Use of AI is optional; many successful
l10n teams work well without it.

The section "Housekeeping tasks for localization workflows" documents the
most commonly used housekeeping tasks.


## Background knowledge for localization workflows

Essential background for the workflows below; understand these concepts before
performing any housekeeping tasks in this document.

### Language code and notation (XX, ll, ll\_CC)

**XX** is a placeholder for the language code: either `ll` (ISO 639) or
`ll_CC` (e.g. `de`, `zh_CN`). It appears in the PO file header metadata
(e.g. `"Language: zh_CN\n"`) and is typically used to name the PO file:
`po/XX.po`.


### Header Entry

The **header entry** is the first entry in every `po/XX.po`. It has an empty
`msgid`; translation metadata (project, language, plural rules, encoding, etc.)
is stored in `msgstr`, as in this example:

```po
msgid ""
msgstr ""
"Project-Id-Version: Git\n"
"Language: zh_CN\n"
"MIME-Version: 1.0\n"
"Content-Type: text/plain; charset=UTF-8\n"
"Content-Transfer-Encoding: 8bit\n"
"Plural-Forms: nplurals=2; plural=(n != 1);\n"
```

**CRITICAL**: Do not edit the header's `msgstr` while translating. It holds
metadata only and must be left unchanged.


## Housekeeping tasks for localization workflows

For common housekeeping tasks, follow the steps in the matching subsection
below.


### Task 1: Generating or updating po/git.pot

When asked to generate or update `po/git.pot` (or the like):

1. **Directly execute** the command `make po/git.pot` without checking
   if the file exists beforehand.

2. **Do not verify** the generated file after execution. Simply run the
   command and consider the task complete.


## Human translators remain in control

Git translation is human-driven; language team leaders and contributors are
responsible for maintaining translation quality and consistency.

AI-generated output should always be treated as drafts that must be reviewed
and approved by someone who understands both the technical context and the
target language. The best results come from combining AI efficiency with human
judgment, cultural insight, and community engagement.
