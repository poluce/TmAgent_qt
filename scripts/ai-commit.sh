#!/usr/bin/env bash
set -euo pipefail

# ai-commit.sh
# Make a git commit with author set to "小五" for changes made by the assistant.
# Usage:
#   ./scripts/ai-commit.sh -m "your message"
#   ./scripts/ai-commit.sh "your message"
# Notes:
# - This does NOT change global git config.
# - Author info is embedded in the commit. Push will then show this author.

AUTHOR_NAME="小五"
AUTHOR_EMAIL="xiaowu@users.noreply.github.com"

msg=""
if [[ $# -ge 2 && "$1" == "-m" ]]; then
  msg="$2"
else
  msg="${1:-}"
fi

if [[ -z "$msg" ]]; then
  echo "ERROR: commit message required" >&2
  echo "Usage: $0 -m \"message\"  (or: $0 \"message\")" >&2
  exit 2
fi

# Ensure we are in a git repo
if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "ERROR: not inside a git repository" >&2
  exit 2
fi

# Stage changes if user forgot
if git diff --quiet && git diff --cached --quiet; then
  echo "Nothing to commit." >&2
  exit 0
fi

# Create commit with explicit author and committer
GIT_AUTHOR_NAME="$AUTHOR_NAME" \
GIT_AUTHOR_EMAIL="$AUTHOR_EMAIL" \
GIT_COMMITTER_NAME="$AUTHOR_NAME" \
GIT_COMMITTER_EMAIL="$AUTHOR_EMAIL" \
  git commit --author="$AUTHOR_NAME <$AUTHOR_EMAIL>" -m "$msg"
