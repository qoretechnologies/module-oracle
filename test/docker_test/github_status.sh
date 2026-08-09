#!/bin/bash
#
# reports the status of the current GitLab CI job as a commit status on the GitHub mirror
#
# usage: github_status.sh <pending|success|failure|error>

if [ -z "$1" ]; then
    echo "usage: `basename $0` <pending|success|failure|error>" >&2
    exit 1
fi

curl "https://api.github.com/repos/qoretechnologies/${REPO_NAME}/statuses/${CI_COMMIT_SHA}" \
    -X POST --oauth2-bearer "${GITHUB_ACCESS_TOKEN}" -H "Content-Type: application/json" \
    -d "{\"state\": \"$1\", \"context\": \"${REPO_NAME}\", \"description\": \"Gitlab CI\", \"target_url\": \"${CI_JOB_URL}\"}"
