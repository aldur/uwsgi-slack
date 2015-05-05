# uWSGI-slack
uWSGI-slack is a plugin that allows alarms/hooks integration with the [Slack](https://www.slack.com/) service.

## Features
uWSGI-slack provides the following features:

* Registers _slack alarm_ and _slack hook_.
* Support to the [Slack incoming webhooks API](https://api.slack.com/incoming-webhooks) (send messages to channels, users and so on).

## Installation
This plugin requires:
* [libcurl](http://curl.haxx.se/libcurl/) (to send HTTP requests with ease)
* [jansson](https://github.com/akheron/jansson) (for JSON parsing)

Please follow the specific documentation on how to install them.

This plugin is 2.0 friendly.
You can thus build it with:
```bash
$ git clone https://github.com/aldur/uwsgi-slack
$ uwsgi --build-plugin uwsgi-slack
```

## Configuration
To use this plugin you'll need to setup an [incoming webhook integration](https://my.slack.com/services/new/incoming-webhook/) in your Slack team.

You can configure the alarms in your app as follows:
```ini
[uwsgi]
plugins = slack

; register a 'slackme' alarm
alarm = slackme slack:webhook_url=YOUR_SLACK_TEAM_WEBHOOK_URL,username=uWSGI Alarmer,icon_emoji=:heavy_exclamation_mark:
; raise alarms no more than 1 time per minute (default is 3 seconds)
alarm-freq = 60

; raise an alarm whenever uWSGI segfaults
alarm-segfault = slackme

; raise an alarm whenever /danger is hit
route = ^/danger alarm:slackme /danger has been visited !!!

; raise an alarm when the avergae response time is higher than 3000 milliseconds
metric-alarm = key=worker.0.avg_response_time,value=3000,alarm=slackme

; ...
```
The only mandatory key-value field we need to send an alarm is the `webhook_url`.
In the previous example we've also set the Slack BOT username and it's icon (an emoji).

Hooks, on the other side, require the message text too:
```ini
[uwsgi]
plugins = slack

hook-post-app = slack:webhook_url=YOUR_SLACK_TEAM_WEBHOOK_URL,text=Your awesome app has just been loaded!,username=Your friendly neighbourhood Flip-Man,icon_emoji=:flipper:

; ...
```

### Key-value fields
We support the entire [Slack incoming webhook API](https://api.slack.com/incoming-webhooks).
Available key-values fields include:
* text
* channel
* username
* icon_emoji
* icon_url

Unspecified settings will fallback to the Slack webhook defaults.

On the uWSGI/networking side you can set:
* timeout: specifies the socket timeout.
* ssl_no_verify: tells Curl to not verify the server SSL certificate.

### Future improvements
Support Slack [Message Attachments](https://api.slack.com/docs/attachments).
