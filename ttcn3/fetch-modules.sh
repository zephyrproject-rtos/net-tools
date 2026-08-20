#!/bin/sh
# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0
#
# Clone the third party TTCN-3 modules listed in modules.txt into modules/,
# each checked out at its pinned commit. Re-running is cheap: a repository that
# is already at the right commit is left alone.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
modules_dir="$here/modules"
url_base="https://gitlab.eclipse.org/eclipse/titan"

mkdir -p "$modules_dir"

while read -r repo commit; do
	case "$repo" in
	''|\#*) continue ;;
	esac

	dest="$modules_dir/$repo"

	if [ -d "$dest/.git" ]; then
		if [ "$(git -C "$dest" rev-parse HEAD)" = "$commit" ]; then
			echo "$repo: already at $commit"
			continue
		fi
	else
		echo "$repo: cloning"
		git clone -q "$url_base/$repo.git" "$dest"
	fi

	echo "$repo: checking out $commit"
	git -C "$dest" fetch -q origin
	git -C "$dest" checkout -q "$commit"
done < "$here/modules.txt"

echo "Modules are in $modules_dir"
