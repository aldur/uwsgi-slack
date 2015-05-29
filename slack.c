#include <uwsgi.h>
#include <curl/curl.h>
#include <jansson.h>

#define PNAME "slack"
#define LNAME "[uwsgi-"PNAME"]"

extern struct uwsgi_server uwsgi;

struct uwsgi_slack {
    struct uwsgi_string_list *attachment_s;
    struct slack_attachment *attachments;

    struct uwsgi_string_list *field_s;
    struct slack_field *fields;
} uslack;

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

    // Attachments support
    char *attachments;

    // Other configuration options
    char *ssl_no_verify;
    char *timeout;
};

struct slack_attachment {
    char *name;
    char *fallback;
    char *color;
    char *pretext;

    char *author_name;
    char *author_link;
    char *author_icon;

    char *title;
    char *title_link;

    char *text;

    char *fields;

    char *image_url;
    char *thumb_url;

    struct slack_attachment *next;
};

struct slack_field {
    char *name;

    char *title;
    char *value;
    char sshort;

    struct slack_field *next;
};

static struct uwsgi_option slack_options[] = {
    {"slack-attachment", required_argument, 0,
        "specify a slack attachment", uwsgi_opt_add_string_list, &uslack.attachment_s, 0},
    {"slack-field", required_argument, 0,
        "specify a slack attachment-field", uwsgi_opt_add_string_list, &uslack.field_s, 0},
    UWSGI_END_OF_OPTIONS
};

#define sfkv(x) #x, &f->x
static void slack_add_field(char *arg, size_t arg_len) {
    struct slack_field *f = uwsgi_calloc(sizeof(struct slack_field));

    char *sshort = NULL;
    if (uwsgi_kvlist_parse(arg, arg_len, ',', '=',
                sfkv(name),
                sfkv(title),
                sfkv(value),
                "short", &sshort,
                NULL)
       ){
        uwsgi_log(LNAME" unable to parse slack field definition\n");
        goto shutdown;
    }

    if (!f->name) {
        uwsgi_log(LNAME" a name is required for the field definition\n");
        goto shutdown;
    }

    if (!f->title) {
        uwsgi_log(LNAME" a title is required for the field definition\n");
        goto shutdown;
    }

    if (!f->value) {
        uwsgi_log(LNAME" a value is required for the field definition\n");
        goto shutdown;
    }

    if (sshort) {
         f->sshort = 1;
         free(sshort);
    }

    struct slack_field *uslf = uslack.fields;
    if (!uslf) {
        uslack.fields = f;
    } else {
        while (uslf->next) {
            uslf = uslf->next;
        }

        uslf->next = f;
    }

    return;

shutdown:
    exit(1);
}

static struct slack_field *slack_get_field(char *field_name) {
    struct slack_field *sfield = uslack.fields;

    while (sfield) {
        if (strcmp(sfield->name, field_name) == 0) {
            return sfield;
        }

        sfield = sfield->next;
    }

    return NULL;
}

static struct slack_attachment *slack_get_attachment(char *attachment_name) {
    struct slack_attachment *sattachment = uslack.attachments;

    while (sattachment) {
        if (strcmp(sattachment->name, attachment_name) == 0) {
            return sattachment;
        }

        sattachment = sattachment->next;
    }

    return NULL;
}

#define sakv(x) #x, &attachment->x
static void slack_add_attachment(char *arg, size_t arg_len) {
    struct slack_attachment *attachment = uwsgi_calloc(sizeof(struct slack_attachment));

    if (uwsgi_kvlist_parse(arg, arg_len, ',', '=',
                sakv(name),
                sakv(fallback),
                sakv(color),
                sakv(pretext),
                sakv(author_name),
                sakv(author_link),
                sakv(author_icon),
                sakv(title),
                sakv(title_link),
                sakv(text),
                sakv(fields),
                sakv(image_url),
                sakv(thumb_url),
                NULL)
       ){
        uwsgi_log(LNAME" unable to parse slack attachment definition\n");
        goto shutdown;
    }

    if (!attachment->name) {
        uwsgi_log(LNAME" a name is required for the attachment definition\n");
        goto shutdown;
    }

    struct slack_attachment *uslas = uslack.attachments;
    if (!uslas) {
        uslack.attachments = attachment;
    } else {
        while (uslas->next) {
            uslas = uslas->next;
        }

        uslas->next = attachment;
    }

    return;

shutdown:
    exit(1);
}

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
                pkv(timeout),
                pkv(attachments),
                NULL)) {
        uwsgi_log(LNAME" unable to parse specified Slack options\n");
        return -1;
    }

    if (!pbc->webhook_url) {
        uwsgi_log(LNAME" you need to specify a Slack Webhook URL\n");
        return -1;
    }

    return 0;
}

#define sfree(x) if(pbc->x) free(pbc->x)
static void slack_free(struct slack_config *pbc) {
    sfree(webhook_url);
    sfree(text);
    sfree(channel);
    sfree(username);
    sfree(attachments);
    sfree(icon_emoji);
    sfree(icon_url);
    sfree(ssl_no_verify);
    sfree(timeout);
}

static json_t *build_field_json(struct slack_field *f) {
    json_t *jf = json_object();

    if (json_object_set_new(jf, "title", json_string(f->title))) goto error;
    if (json_object_set_new(jf, "value", json_string(f->value))) goto error;

    if (f->sshort) {
        if (json_object_set_new(jf, "short", json_true())) goto error;
    }

    return jf;

error:
    uwsgi_log(LNAME" error while building Slack field JSON\n");
    return NULL;
}

#define saj(x) if (a->x && json_object_set_new(ja, #x, json_string(a->x))) goto error
static json_t *build_attachment_json(struct slack_attachment *a) {
    json_t *ja = json_object();

    saj(fallback);
    saj(color);
    saj(pretext);
    saj(author_name);
    saj(author_link);
    saj(author_icon);
    saj(title);
    saj(title_link);
    saj(text);
    saj(image_url);
    saj(thumb_url);

    if (a->fields) {
        // We're gonna fill the attachment fields
        json_t *jfs = json_array();
        char *field_name = uwsgi_concat2n(a->fields, strlen(a->fields), "", 0);
        char *field_name_f = field_name;

        char *semicolon;
        do {
            // Required field-names are divided by a semicolon
            semicolon = strchr(field_name, ';');

            char *field_name_t = field_name;
            if (semicolon) {
                *semicolon = 0;
                field_name = ++semicolon;
            }

            struct slack_field *f;
            if (!(f = slack_get_field(field_name_t))) {
                uwsgi_log(LNAME" unable to find required attachment-field name\n");
                goto error;
            }

            json_t *jf = build_field_json(f);
            if (!jf || json_array_append_new(jfs, jf)) goto error;
        } while (semicolon);

        if (json_object_set_new(ja, "fields", jfs)) goto error;
        free(field_name_f);
    }

    return ja;

error:
    uwsgi_log(LNAME" error while building Slack attachment JSON\n");
    return NULL;
}

#define sj(x) if (pbc->x && json_object_set_new(j, #x, json_string(pbc->x))) goto error
static json_t *build_json(struct slack_config *pbc, char* text) {
    json_t *j = json_object();

    if (text) {
        if (json_object_set_new(j, "text", json_string(text))) goto error;
    } else {
        if (json_object_set_new(j, "text", json_string(pbc->text))) goto error;
    }

    sj(channel);
    sj(username);

    if (pbc->icon_emoji) {
        if (json_object_set_new(j, "icon_emoji", json_string(pbc->icon_emoji))) goto error;
    } else if (pbc->icon_url) {
        if (json_object_set_new(j, "icon_url", json_string(pbc->icon_url))) goto error;
    }

    // We'll handle the attachments here.
    if (pbc->attachments) {
        char *attachments = uwsgi_concat2n(pbc->attachments, strlen(pbc->attachments), "", 0);
        char *attachments_f = attachments;

        json_t *jas = json_array();

        char *semicolon;
        do {
            // Required attachment-names are divided by a semicolon
            semicolon = strchr(attachments, ';');

            char *attachments_t = attachments;
            if (semicolon) {
                *semicolon = 0;
                attachments = ++semicolon;
            }

            struct slack_attachment *a;
            if (!(a = slack_get_attachment(attachments_t))) {
                uwsgi_log(LNAME" unable to find required attachment name %s\n",
                        attachments_t);
                goto error;
            }

            json_t *ja = build_attachment_json(a);
            if (!ja || json_array_append_new(jas, ja)) goto error;

        } while (semicolon);

        if (json_object_set_new(j, "attachments", jas)) goto error;
        free(attachments_f);
    }

    return j;

error:
    uwsgi_log(LNAME" error while building Slack JSON\n");
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
        uwsgi_log(LNAME" curl_easy_init error\n");
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
        uwsgi_log(LNAME" libcurl error: %s\n", curl_easy_strerror(res));
        ret = -1;
        goto cleanup;
    } else if (http_code != 200){
        uwsgi_log(LNAME" Slack returned wrong HTTP status code: %d\n", http_code);
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
        uwsgi_log(LNAME" you need to specify the Slack message text for hooks\n");
        goto clear;
    }

    ret = slack_request(&pbc, NULL);

clear:
    slack_free(&pbc);
    return ret;
}

static void slack_alarm_func(struct uwsgi_alarm_instance *uai, char *msg, size_t len) {
    struct slack_config *pbc = (struct slack_config *)uai->data_ptr;

    char *text = uwsgi_concat2n(msg, len, "", 0);
    slack_request(pbc, text);
    free(text);
}

static void slack_alarm_init(struct uwsgi_alarm_instance *uai) {
    struct slack_config *pbc = uwsgi_calloc(sizeof(struct slack_config));

    if (slack_config_do(uai->arg, pbc)) {
        exit(1);
    }

    uai->data_ptr = pbc;
}

static void slack_register() {
    uwsgi_register_hook(PNAME, slack_hook);
    uwsgi_register_alarm(PNAME, slack_alarm_init, slack_alarm_func);
}

static int slack_init() {
    struct uwsgi_string_list *usl = uslack.field_s;
    while (usl) {
         slack_add_field(usl->value, usl->len);
         usl = usl->next;
    }

    usl = uslack.attachment_s;
    while (usl) {
        slack_add_attachment(usl->value, usl->len);
        usl = usl->next;
    }

    return 0;
}

struct uwsgi_plugin slack_plugin = {
    .name = PNAME,
    .init = slack_init,
    .options = slack_options,
    .on_load = slack_register,
};
