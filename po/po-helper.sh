#!/bin/sh
#
# Copyright (c) 2012 Jiang Xin

PODIR=$(git rev-parse --show-cdup)po
POTFILE=$PODIR/git.pot
TEAMSFILE=$PODIR/TEAMS

usage () {
	cat <<-\END_OF_USAGE
Maintaince script for l10n files and commits.

Usage:

 * po-helper.sh init XX.po
       Create the initial XX.po file in the po/ directory, where
       XX is the locale, e.g. "de", "is", "pt_BR", "zh_CN", etc.

 * po-helper.sh update XX.po ...
       Update XX.po file(s) from the new git.pot template

 * po-helper.sh check
       Check XX.po files as well as commits.

 * po-helper.sh check XX.po ...
       Perform syntax check on XX.po file(s)

 * po-helper.sh check commit <commit>
 * po-helper.sh check commits <since> [<til>]
       Check proper encoding of non-ascii chars in commit logs

       - don't write commit log with non-ascii chars without proper
         encoding settings;

       - subject of commit log must written in English; and

       - don't change files outside this directory (po/)

 * po-helper.sh diff [<old> <new>]
       Show difference between old and new po/pot files.
       Default show changes of git.pot since last update.
END_OF_USAGE

	if test $# -gt 0
	then
		echo >&2
		echo >&2 "Error: $*"
		exit 1
	else
		exit 0
	fi
}

die () {
	echo >&2 "$@"
	exit 1
}

# Init or update XX.po file from git.pot
update_po () {
	if test $# -eq 0
	then
		usage "init/update needs at least one argument"
	fi
	for locale
	do
		locale=${locale##*/}
		locale=${locale%.po}
		po=$PODIR/$locale.po
		mo=$PODIR/build/locale/$locale/LC_MESSAGES/git.mo
		if test -n "$locale"
		then
			if test -f "$po"
			then
				msgmerge --add-location --backup=off -U "$po" "$POTFILE"
				mkdir -p "${mo%/*}"
				msgfmt -o "$mo" --check --statistics "$po"
			else
				msginit -i "$POTFILE" --locale="$locale" -o "$po"
				perl -pi -e 's/(?<="Project-Id-Version: )PACKAGE VERSION/Git/' "$po"
				notes_for_l10n_team_leader "$locale"
			fi
		fi
	done
}

notes_for_l10n_team_leader () {
	cat <<-END_OF_NOTES
	============================================================
	Notes for l10n team leader:

	    Since you create a initialial locale file, you are
	    likely to be the leader of the $1 l10n team.

	    You can add your team infomation in the "po/TEAMS"
	    file, and update it when necessary.

	    Please read the file "po/README" first to understand
	    the workflow of Git l10n maintenance.
	============================================================
	END_OF_NOTES
}

# Check po files and commits. Run all checks if no argument is given.
check () {
	if test $# -eq 0
	then
		echo "------------------------------------------------------------"
		ls $PODIR/*.po |
		while read f
		do
			printf "%-10s: %s\n" "${f##*/}" "$(check "$f" 2>&1)"
		done

		echo "------------------------------------------------------------"
		echo "Show updates of git.pot..."
		echo
		show_diff

		echo "------------------------------------------------------------"
		echo "Check commits..."
		echo
		check commits
		echo "------------------------------------------------------------"
		echo "Note: If you want to check for upstream l10n update, run:"
		echo "Note:"
		echo "Note:     po-helper.sh check update <remote>"
		echo "------------------------------------------------------------"
	fi
	while test $# -gt 0
	do
		case "$1" in
		*.po)
			check_po "$1"
			;;
		commit | commits)
			shift
			check_commits "$@"
			break
			;;
		update)
			shift
			if test $# -eq 0
			then
				echo "Input remote name on which you want to check for l10n update:"
				read remote
			else
				remote="$1"
			fi
			if test -z "$remote"
			then
				echo >&2 "Must provides a valid remote name."
			elif git remote | grep -q "^$remote$"
			then
				check_upstream_update "$remote"
			else
				echo >&2 "Remote \"$remote\" does not exist."
			fi
			break
			;;
		*)
			usage "Unkown task '$1' for check"
			;;
		esac
		shift
	done
}

# Syntax check on XX.po
check_po () {
	for locale
	do
		locale=${locale##*/}
		locale=${locale%.po}
		po=$PODIR/$locale.po
		mo=$PODIR/build/locale/$locale/LC_MESSAGES/git.mo
		if test -n "$locale"
		then
			if test -f "$po"
			then
				mkdir -p "${mo%/*}"
				msgfmt -o "$mo" --check --statistics "$po"
			else
				echo >&2 "Error: File $po does not exist."
			fi
		fi
	done
}

# Show differences between 2 po/pot files
# Return 1 if find any difference(s)
po_diff_stat () {
	left=$1
	right=$2
	test -f "$left"  || die "File $left not exist!"
	test -f "$right" || die "File $right not exist!"

	pnew="^.*:\([0-9]*\): this message is used but not defined in.*"
	pdel="^.*:\([0-9]*\): warning: this message is not used.*"
	new_count=0
	del_count=0
	diffstat=

	LANGUAGE=C msgcmp -N --use-untranslated "$left" "$right" 2>&1 | {
		while read line
		do
			# New message example:
			#     git.pot:NNN: this message is used but not defined in /tmp/git.po.XXXX
			m=$(echo $line | grep "$pnew" | sed -e "s/$pnew/\1/")
			if test -n "$m"
			then
				new_count=$(( new_count + 1 ))
				continue
			fi

			# Delete message example:
			#     /tmp/git.po.XXXX:NNN: warning: this message is not used
			m=$(echo $line | grep "$pdel" | sed -e "s/$pdel/\1/")
			if test -n "$m"
			then
				del_count=$(( del_count + 1 ))
			fi
		done
		if test $new_count -eq 0 && test $del_count -eq 0
		then
			return 0
		elif test $new_count -eq 0
		then
			diffstat="$del_count removed"
		elif test $del_count -eq 0
		then
			diffstat="$new_count new"
		else
			diffstat="$new_count new, $del_count removed"
		fi

		echo "$diffstat"
		return $(( new_count + del_count ))
	}
}

# Show summary of updates of git.pot or difference between two po/pot files.
show_diff () {
	tmpfile=

	case $# in
	0 | 1)
		if test $# -eq 1 && test "$1" != "$POTFILE"
		then
			new=$1
			str_from_old="from the orignal '${new##*/}' file "
			str_to_new="in the new '${new##*/}' file "
		else
			new=$POTFILE
			str_from_old="from the previous version "
			str_to_new=""
		fi
		tmpfile=$(mktemp /tmp/git-po.XXXX)
		old=$tmpfile
		status=$(cd $PODIR; git status --porcelain -- ${new##*/})
		if test -z "$status"
		then
			echo >&2 "# Nothing changed. (run 'make pot' first)"
			return 0
		fi
		(cd $PODIR; LANGUAGE=C git show HEAD:./${new##*/} >"$tmpfile")
		# Remove tmpfile on exit
		trap 'rm -f "$tmpfile"' 0
		;;
	2)
		old=$1
		new=$2
		str_from_old="from ${old##*/} "
		str_to_new="in ${new##*/} "
		;;
	*)
		usage "show_diff takes no more than 2 arguments."
		;;
	esac

	diffstat=$(po_diff_stat "$old" "$new")
	if test $? -eq 0
	then
		return 0
	else
		echo >&2 "# Diff between ${old##*/} and ${new##*/}"
		echo >&2
		echo "l10n: git.pot: vN.N.N round N ($diffstat)"
		echo
		echo "Generate po/git.pot from $(git describe --always) for git vN.N.N l10n round N."
		return 1
	fi
}

verify_commit_encoding () {
	c=$1
	subject=0
	non_ascii=""
	encoding=""
	log=""

	while read line
	do
		log="$log - $line"
		# next line would be the commit log subject line,
		# if no previous empty line found.
		if test -z "$line"
		then
			subject=$(( subject + 1 ))
			continue
		fi
		if test $subject -eq 0
		then
			if echo $line | grep -q "^encoding "
			then
				encoding=${line#encoding }
			fi
		fi
		# non-ascii found in commit log
		m=$(echo $line | sed -e "s/\([[:alnum:][:space:][:punct:]]\)//g")
		if test -n "$m"
		then
			non_ascii="$m >> $line <<"
			if test $subject -eq 1
			then
				report_nonascii_in_subject $c "$non_ascii"
				return
			fi
		fi
		# subject has only one line
		test $subject -eq 1 && subject=$(( subject + 1 ))
		# break if there are non-asciis and has already checked subject line
		if test -n "$non_ascii" && test $subject -gt 0
		then
			break
		fi
	done
	if test -n "$non_ascii"
	then
		if test -z "$encoding"
		then
			echo $line | iconv -f UTF-8 -t UTF-8 -s >/dev/null ||
				report_bad_encoding "$c" "$non_ascii"
		else
			echo $line | iconv -f $encoding -t UTF-8 -s >/dev/null ||
				report_bad_encoding "$c" "$non_ascii" "$encoding"
		fi
	fi
}

verify_commit_log () {
	c=$1
	subject=0
	subject_lines=0

	while read line
	do
		if test $subject -eq 0
		then
			# The first blank line seperate commit object headings
			# and log messages"
			if test -z "$line"
			then
				subject=$(( subject + 1 ))
			fi
			continue
		fi

		# Subject line should no longger than 50 characters and
		# should not end with a punctuation.
		if test $subject -eq 1
		then
			if test -n "$line"
			then
				subject_lines=$(( subject_lines + 1 ))
			else
				subject=$(( subject + 1 ))
				continue
			fi
			if test $subject_lines -gt 1
			then
				echo >&2 "Error: in commit $c, multiple lines found in subject."
				subject=$(( subject + 1 ))
			else
				if test "${line%.}" != "$line"
				then
					echo >&2 "Error: in commit $c, subject should not end with a punctuation."
					echo >&2 "       \"$line\""
				fi
				if test ${#line} -gt 50
				then
					echo >&2 "Error: in commit $c, subject should less than 50 characters."
					echo >&2 "       \"$line\""
				fi
			fi
		fi
		# Description in commit log should line wrap at 72 characters.
		if test $subject -gt 1
		then
			if test ${#line} -gt 72
			then
				echo >&2 "Error: in commit $c, description should line wrap at 72 characters."
				echo >&2 "       \"$line\""
			fi
		fi
	done
}

verify_commit_log_sob () {
	c=$1
	sob=""

	while read line
	do

		if test -z "$line"
		then
			break
		fi
		if test "${line#Signed-off-by: }" != "$line"
		then
			sob=$line
		fi
		if ! echo $line | grep -q "^.*-by: .\{1,\} <.\{1,\}>"
		then
			echo >&2 "Error: in commit $c, no s-o-b or bad s-o-b: $line"
		fi
	done
	if test -z "$sob"
	then
		echo >&2 "Error: in commit $c, there should have a 'Signed-off-by:' line."
	fi
}

is_merge_commit()
{
	c=$1
	parents=$(git cat-file commit $c | grep "^parent [0-9a-f]\{40\}" | wc -l)
	if test $parents -ge 2
	then
		true
	else
		false
	fi
}

report_nonascii_in_subject () {
	c=$1
	non_ascii=$2

	echo >&2 "============================================================"
	echo >&2 "Error: Non-ASCII in subject of commit $c:"
	echo >&2 "       ${non_ascii}"
	echo >&2
	git cat-file commit "$c" | head -15 |
	while read line
	do
		echo >&2 "\t$line"
	done
	echo >&2
}

report_bad_encoding () {
	c=$1
	non_ascii=$2
	encoding=$3

	echo >&2 "============================================================"
	if test -z "$encoding"
	then
		echo >&2 "Error: Not have encoding setting for commit $c:"
	else
		echo >&2 "Error: Wrong encoding ($encoding) for commit $c:"
	fi
	echo >&2 "       ${non_ascii}"
	echo >&2
	git cat-file commit "$c" | head -15 |
	while read line
	do
		echo >&2 "\t$line"
	done
	echo
}

# Check commit logs for bad encoding settings
check_commits () {
	if test $# -gt 2
	then
		usage "check commits only needs 2 arguments"
	fi
	. $(git --exec-path)/git-parse-remote
	if test $# -eq 1
	then
		since=${1}~1
		til=${1}
	else
		since=${1:-$(get_remote_merge_branch)}
		til=${2:-$(git symbolic-ref -q HEAD)}
	fi
	if git diff-tree -r "$since" "$til" | awk '{print $6}' | grep -qv "^po/"
	then
		echo >&2 "============================================================"
		echo >&2 "Error: changed files outside po directory!"
		echo >&2 "       reference: git diff-tree -r $since $til"
	fi

	count=0
	git rev-list ${since}..${til} | {
		while read c
		do
			cobject=$(git cat-file commit $c)
			echo "$cobject" | verify_commit_encoding $c
			echo "$cobject" | verify_commit_log $c
			if ! is_merge_commit $c
			then
				echo "$cobject" | tac | verify_commit_log_sob $c
			fi
			count=$(( count + 1 ))
		done
		echo "$count commits checked complete."
	}
}

# Check whether upstream master/next branches have new l10n strings
# Default remote: kernel
check_upstream_update () {
	if test $# -ne 1
	then
		if git remote | grep "^kernel$"
		then
			remote="kernel"
		else
			die "Check which <remote> ?"
		fi
	elif test "$1" = "origin"
	then
		die "<remote> can not be 'origin'"
	else
		remote=$1
	fi

	# Must in top dir
	test -f "po/git.pot" || die "File po/git.pot does not exist!"

	# Fetch form origin and <remote>, which can be done manually.
	#git fetch
	#git fetch $remote

	# Validate remote branch: $remote/master, and $remote/next
	if ! git rev-parse "remotes/$remote/master" "remotes/$remote/next" >/dev/null 2>&1
	then
		echo >&2 "Required branch master and/or next not exist in $remote"
		exit 1
	fi

	# Save current branch and save current git.pot as $working
	current_branch=$(git symbolic-ref --short HEAD 2>/dev/null || \
	                 git rev-parse HEAD)
	working=$(mktemp /tmp/git-pot.XXXX)
	cp po/git.pot $working
	# Stash all files include ignore files, (such as config.mak.autogen,
	# config.status), which may break "make pot" latter.
	git stash save --all "po-helper: check for update at $(date +'%Y-%m-%d %H:%M:%S')" >/dev/null
	trap "git reset --hard >/dev/null 2>&1;
	      git checkout $current_branch >/dev/null 2>&1;
	      git clean -fdx >/dev/null;
	      git stash pop >/dev/null;
	      rm -f \"$working\"" 0

	result=0
	for branch in "remotes/$remote/master" "remotes/$remote/next"
	do
		git checkout --quiet $branch
		touch -t 200504071513.13 po/git.pot
		make pot >/dev/null 2>&1
		diffstat=$(po_diff_stat "$working" po/git.pot)
		error_code=$?
		if test $error_code -ne 0
		then
			result=1
			echo "New l10n updates found in \"$(basename $branch)\" branch of remote \"<$remote>\":"
			if test $error_code -eq 1
			then
				echo "    $diffstat message."
			else
				echo "    $diffstat messages."
			fi
			echo
		fi
		git checkout -- po/git.pot
	done
	return $result
}

# Show leader or members of l10n team(s)
show_team () {
	role="all"
	team=""

	case $# in
	0)
		;;
	1)
		case $1 in
		leader*)
			role="leader"
			;;
		member* | all)
			role=$1
			;;
		*)
			team=$1
			;;
		esac
		;;
	2)
		case $1 in
		leader*)
			role="leader"
			team=$2
			;;
		member* | all)
			role=$1
			team=$2
			;;
		*)
			team=$1
			role=$2
			;;
		esac
		;;
	*)
		usage "show_team takes no more than 2 arguments."
		;;
	esac

	test "$role" = "leader" || role="all"

	if test ! -f $TEAMSFILE
	then
		echo >&2 "TEAMS file not found."
		exit 1
	fi

	while read line
	do
		test "${line#\#}" != "$line" && continue
		if test "${line%:*}" = "Language"
		then
			ct=$(echo $line | sed -e 's/^Language:[[:space:]]*\([^[:space:]]*\)[[:space:]]*(.*)$/\1/g')
			if test -z "$team" || test "$team" = "$ct"
			then
				while read line
				do
					test -z "$line" && break
					if test "${line%:*}" = "Leader"
					then
						cl=$(echo $line | sed -e 's/^Leader:[[:space:]]*//' -e 's/ AT /@/g')
						if test "$role" = "leader" || test "$role" = "all"
						then
							echo $cl,
						fi
					elif test "${line%:*}" = "Members"
					then
						cm=$(echo $line | sed -e 's/^Members:[[:space:]]*//' -e 's/ AT /@/g')

						if test "$role" = "all"
						then
							echo $cm,
						fi
					elif test "${line%:*}" = "$line"
					then
						if test "$role" = "all"
						then
							echo $line, | sed -e 's/ AT /@/g'
						fi
					fi
				done
				test -n "$team" && break
			fi
		fi
	done < $TEAMSFILE

}

#############################################################################

test $# -eq 0 && usage

if ! test -f "$POTFILE"
then
	echo "Cannot find git.pot in your workspace. Are you in the workspace of git project?"
	exit 1
fi

while test $# -ne 0
do
	case "$1" in
	init | update)
		shift
		update_po "$@"
		break
		;;
	check)
		shift
		check "$@"
		break
		;;
	diff)
		shift
		show_diff "$@"
		break
		;;
	team | teams)
		shift
		show_team "$@"
		break
		;;
	*.po)
		update_po "$1"
		;;
	-h | --help)
		usage
		;;
	*)
		usage "Unknown command '$1'."
		;;
	esac
	shift
done
