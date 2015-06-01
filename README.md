# uWSGI-slack
uWSGI-slack is a plugin that allows alarms/hooks integration with the [Slack](https://www.slack.com/) service.

## Features
uWSGI-slack provides the following features:

* Registers _slack alarm_ and _slack hook_.
* Support to the [Slack incoming webhooks API](https://api.slack.com/incoming-webhooks) (send messages to channels, users and so on).
* Support to the [Slack messagge attachments API](https://api.slack.com/docs/attachments) (fancy message attachments).

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

### Alarms/hooks key-value options
We support the entire [Slack incoming webhook API](https://api.slack.com/incoming-webhooks).
Available key-values options are:

* text
* channel
* username
* icon_emoji
* icon_url

Unspecified settings will fallback to the Slack webhook defaults.

As an extra, you can specify a `;` separated list of [attachments](#attachments) as a value for the `attachments` key.

### Attachments
You can define an attachment and send messages containing it by using the following configuration snippet:

```ini
[uwsgi]
plugins = slack

slack-attachment = name=groove,title=A slack attachment,color=#7CD197
hook-post-app = slack:webhook_url=YOUR_SLACK_TEAM_WEBHOOK_URL,attachments=groove,text=Hook text

; ...
```

As you can see we specify a mandatory attachment `name`, we setup the attachment and we link it to the uWSGI hook / alarm through the `attachments` key.

__Note:__ the attachments' name lookup is done at runtime. A malformed attachment name will let uWSGI ignore the whole alarm / hook trigger.

Attachments key-value options are:

* name (_mandatory_)
* fallback
* color
* pretext
* author_name
* author_link
* author_icon
* title
* title_link
* text
* image_url
* thumb_url

A detailed explanation for each key can be found [here](https://api.slack.com/docs/attachments).
Again, unspecified settings will fallback to Slack defaults.

As an extra, you can link a `;` separated list of [fields](#fields) to the attachment by using the `fields` key.

### Fields
Fields are nested within attachments and will be displayed in a table.

Their configuration is should look familiar:
```ini
[uwsgi]
plugins = slack

slack-field = name=project,title=Project,value=Awesome Project,short=true
slack-field = name=environment,title=Environment,value=Production Project

slack-attachment = name=groove,title=A slack attachment,color=#7CD197,fields=project;environment

hook-post-app = slack:webhook_url=YOUR_SLACK_TEAM_WEBHOOK_URL,attachments=groove,text=Hook text

; ...
```

Fields key-value options are:

* name (_mandatory_)
* title
* value
* short (if anything is set will be considered True, False otherwise)

As usual, better documentation for each key can be found [here](https://api.slack.com/docs/attachments).

### uWSGI related key-value options
On the uWSGI/networking side you can set:
* timeout: specifies the socket timeout.
* ssl_no_verify: tells Curl to not verify the server SSL certificate.

## Screens
An alarm-resulting example with attachments and fields in action.

![Example](http://i.imgur.com/VZd2auX.png)
