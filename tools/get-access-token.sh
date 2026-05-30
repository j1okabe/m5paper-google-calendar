#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")"; pwd)
ENV_FILE="$SCRIPT_DIR/../.env"
PORT="${PORT:-8080}"
REDIRECT_URI="http://127.0.0.1:${PORT}/"

if [ ! -f "$ENV_FILE" ]; then
    echo ".env not found: $ENV_FILE" >&2
    exit 1
fi

set -a
# shellcheck source=/dev/null
. "$ENV_FILE"
set +a

for command in curl jq nc; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "$command is required" >&2
        exit 1
    fi
done

if [ -z "${GOOGLE_APP_CLIENT_ID:-}" ] || [ -z "${GOOGLE_APP_CLIENT_SECRET:-}" ]; then
    echo "GOOGLE_APP_CLIENT_ID and GOOGLE_APP_CLIENT_SECRET are required in .env" >&2
    exit 1
fi

# Scope list:
# https://developers.google.com/calendar/api/auth
scopes=(
    "https://www.googleapis.com/auth/calendar.events.readonly"
)

scope=""
for url in "${scopes[@]}"; do
    if [ -n "$scope" ]; then
        scope="$scope "
    fi
    scope="$scope$url"
done

authorizationUrl="https://accounts.google.com/o/oauth2/v2/auth"
authorizationUrl="${authorizationUrl}?response_type=code"
authorizationUrl="${authorizationUrl}&client_id=$(jq -nr --arg v "$GOOGLE_APP_CLIENT_ID" '$v|@uri')"
authorizationUrl="${authorizationUrl}&redirect_uri=$(jq -nr --arg v "$REDIRECT_URI" '$v|@uri')"
authorizationUrl="${authorizationUrl}&access_type=offline"
authorizationUrl="${authorizationUrl}&prompt=consent"
authorizationUrl="${authorizationUrl}&scope=$(jq -nr --arg v "$scope" '$v|@uri')"

echo "Open this URL in your browser:"
echo "$authorizationUrl"
echo ""
echo "Waiting for OAuth redirect on $REDIRECT_URI ..."

request=$(
    {
        printf 'HTTP/1.1 200 OK\r\n'
        printf 'Content-Type: text/html; charset=utf-8\r\n'
        printf 'Connection: close\r\n'
        printf '\r\n'
        printf '<html><body>Authorization finished. You can close this tab.</body></html>\r\n'
    } | nc -l 127.0.0.1 "$PORT"
)

request_line=$(printf '%s\n' "$request" | head -n 1)
code=$(printf '%s' "$request_line" | sed -n 's/.*[?&]code=\([^& ]*\).*/\1/p')
error=$(printf '%s' "$request_line" | sed -n 's/.*[?&]error=\([^& ]*\).*/\1/p')
code=${code//+/ }
code=$(printf '%b' "${code//%/\\x}")

if [ -n "$error" ]; then
    echo "OAuth authorization failed: $error" >&2
    exit 1
fi

if [ -z "$code" ]; then
    echo "Authorization code was not found in the redirect request" >&2
    echo "$request_line" >&2
    exit 1
fi

json=$(
    curl -sS \
        --data-urlencode "code=$code" \
        --data-urlencode "client_id=$GOOGLE_APP_CLIENT_ID" \
        --data-urlencode "client_secret=$GOOGLE_APP_CLIENT_SECRET" \
        --data-urlencode "redirect_uri=$REDIRECT_URI" \
        --data-urlencode "grant_type=authorization_code" \
        https://oauth2.googleapis.com/token
)

refreshToken=$(printf '%s' "$json" | jq -r '.refresh_token // empty')
accessToken=$(printf '%s' "$json" | jq -r '.access_token // empty')

if [ -z "$refreshToken" ]; then
    echo "Refresh token was not returned." >&2
    echo "Token endpoint response:" >&2
    printf '%s\n' "$json" | jq . >&2
    exit 1
fi

echo "access token"
echo "$accessToken"
echo ""
echo "refresh token"
echo "$refreshToken"
