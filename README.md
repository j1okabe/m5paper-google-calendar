# Overview

Display your google calendar on M5Paper!


# Configurations

## Google Calendar

You need to create Google Calendar App and OAuth

1. Go to https://console.cloud.google.com/apis/dashboard .
2. search Google Calendar and enable it.
3. Go to https://console.cloud.google.com/apis/credentials/oauthclient .
4. Create your OAuth client ID as a `Desktop app`.
5. Copy Client ID and Client Secret.

Now you have Client ID and Client Secret.
Next,

Copy `.env.sample` to `.env`, and open `.env` with editor.
Set `GOOGLE_APP_CLIENT_ID` and `GOOGLE_APP_CLIENT_SECRET`.

So you can now create your refresh token.

```
$ bash tools/get-access-token.sh
...
Open this URL in your browser:
https://accounts.google.com/o/oauth2/v2/auth?response_type=code&...

Waiting for OAuth redirect on http://127.0.0.1:8080/ ...
refresh token
<refresh token>
```

You've got refresh token.

The token tool uses the loopback redirect `http://127.0.0.1:8080/`.
Use a Desktop app OAuth client for this flow.

## setup your wifi and calendar configs

Copy `src\config.cpp.sample` to `src\config.cpp` and write your settings.

- WIFI_SSID
- WIFI_PASSWORD
- GOOGLE_REFRESH_TOKEN
    - your refresh token
- GOOGLE_APP_CLIENT_ID
- GOOGLE_APP_CLIENT_SECRET
- GOOGLE_CALENDAR_ID
    - your calendar ID (typically your mail address)

# build and upload

Use VSCode and Platform-IO plugin.

- board
    - m5stack_paper
- dependent libraries
    - M5GFX
    - M5Unified
    - ArduinoJson

# show wide characters

This project uses M5GFX's built-in Japanese efont.
