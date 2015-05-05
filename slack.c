#include <uwsgi.h>
#include <curl/curl.h>
#include <jansson.h>

extern struct uwsgi_server uwsgi;

struct slack_config {
    // Slack Incoming Webhook URL
    char *webhook_url;

    // Content
    char *text;

    // Destination
    char *channel;

    // Appearance
    char *username;
    char *icon_emoji;
    char *icon_url;

    // Other configuration options
    char *ssl_no_verify;
    char *timeout;
};

#define pkv(x) #x, &pbc->x
static int slack_config_do(char *arg, struct slack_config *pbc) {
    memset(pbc, 0, sizeof(struct slack_config));

    if (uwsgi_kvlist_parse(arg, strlen(arg), ',', '=',
                pkv(webhook_url),
                pkv(text),
                pkv(channel),
                pkv(username),
                pkv(icon_emoji),
                pkv(icon_url),
                pkv(ssl_no_verify),
                pkv(timeout),
                NULL)) {
        uwsgi_log("[uwsgi-slack] unable to parse specified Slack options\n");
        return -1;
    }

    if (!pbc->webhook_url) {
        uwsgi_log("[uwsgi-slack] you need to specify a Slack Webhook URL\n");
        return -1;
    }

    return 0;
}

static void slack_free(struct slack_config *pbc) {
    if (pbc->webhook_url) free(pbc->webhook_url);
    if (pbc->text) free(pbc->text);
    if (pbc->channel) free(pbc->channel);
    if (pbc->username) free(pbc->username);
    if (pbc->icon_emoji) free(pbc->icon_emoji);
    if (pbc->icon_url) free(pbc->icon_url);
    if (pbc->ssl_no_verify) free(pbc->ssl_no_verify);
    if (pbc->timeout) free(pbc->timeout);
}

static json_t *build_json(struct slack_config *pbc, char* text) {
    json_t *j = json_object();

    if (text) {
        if (json_object_set(j, "text", json_string(text))) goto error;
    } else {
        if (json_object_set(j, "text", json_string(pbc->text))) goto error;
    }

    if (pbc->channel) {
        if (json_object_set(j, "channel", json_string(pbc->channel))) goto error;
    }

    if (pbc->username) {
        if (json_object_set(j, "username", json_string(pbc->username))) goto error;
    }

    if (pbc->icon_emoji) {
        if (json_object_set(j, "icon_emoji", json_string(pbc->icon_emoji))) goto error;
    } else if (pbc->icon_url) {
        if (json_object_set(j, "icon_url", json_string(pbc->icon_url))) goto error;
    }


    return j;

error:
    uwsgi_log("[uwsgi-slack] error while creating slack JSON\n");
    return NULL;
}

// Callback to ignore response from server
size_t write_data(void *buffer, size_t size, size_t nmemb, void *userp) {
   return size * nmemb;
}

static int slack_request(struct slack_config *pbc, char* text) {
    int ret = 0;
    char *j_string = NULL;

    struct curl_slist *headerlist = NULL;
    static const char *content_type = "Content-Type: application/json";

    CURL *curl = curl_easy_init();
    if (!curl) {
        uwsgi_log("[uwsgi-slack] curl_easy_init error\n");
        ret = -1;
        goto cleanup;
    }

    // Prepare the header
    headerlist = curl_slist_append(headerlist, content_type);

    // Prepare the text
    json_t *j;
    if (!(j = build_json(pbc, text))) {
        ret = -1;
        goto cleanup;
    }

    j_string = json_dumps(j, 0);
    json_decref(j);

    int timeout = uwsgi.socket_timeout;
    if (pbc->timeout) {
        char *end;
        int t = strtol(pbc->timeout, &end, 10);
        if (!*end) {
            timeout = t;
        }
    }
    char *url = pbc->webhook_url;

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_URL, url);

    if (pbc->ssl_no_verify) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    // CURLOPT_POSTFIELDS sets the request method to POST
    // And the payload to the json string
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, j_string);

    // Set the headers after the HTTP method to overwrite Content Type
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);

    // Set a custom writer to avoid SDOUT
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK) {
        uwsgi_log("[uwsgi-slack] libcurl error: %s\n", curl_easy_strerror(res));
        ret = -1;
        goto cleanup;
    } else if (http_code != 200){
        uwsgi_log("[uwsgi-slack] Slack returned wrong HTTP status code: %d\n", http_code);
        ret = -1;
        goto cleanup;
    }

cleanup:
    curl_easy_cleanup(curl);
    curl_slist_free_all(headerlist);

    if (j_string) {
        free(j_string);
    }

    return ret;
}

static int slack_hook(char *arg) {
    int ret = -1;
    struct slack_config pbc;

    if (slack_config_do(arg, &pbc)) {
        goto clear;
    }

    if (!pbc.text) {
        uwsgi_log("[uwsgi-slack] you need to specify the Slack message text for hooks\n");
        goto clear;
    }

    ret = slack_request(&pbc, NULL);

clear:
    slack_free(&pbc);
    return ret;
}

static void slack_alarm_func(struct uwsgi_alarm_instance *uai, char *msg, size_t len) {
    struct slack_config *pbc = (struct slack_config *)uai->data_ptr;

    char *tmp = uwsgi_concat2n(msg, len, "", 0);
    slack_request(pbc, tmp);
    free(tmp);
}

static void slack_alarm_init(struct uwsgi_alarm_instance *uai) {
    struct slack_config *pbc = uwsgi_calloc(sizeof(struct slack_config));

    if (slack_config_do(uai->arg, pbc)) {
        exit(1);
    }

    uai->data_ptr = pbc;
}

static void slack_register() {
    uwsgi_register_hook("slack", slack_hook);
    uwsgi_register_alarm("slack", slack_alarm_init, slack_alarm_func);
}

struct uwsgi_plugin slack_plugin = {
    .name = "slack",
    .on_load = slack_register,
};
