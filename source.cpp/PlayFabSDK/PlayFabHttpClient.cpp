#include "core/variant.h"
#include "core/io/http_client.h"

typedef void (*UserCallback)();

struct CResult {
    int response_code = 0;
    Dictionary dict_header;
    int size = -1;
    PoolByteArray data;
    int update_count = 0;

    void update(const HTTPClient &client) {
        if (size < 0) {
            response_code = client.get_response_code();
            dict_header = client.get_response_headers_as_dictionary();

            if (!client.is_response_chunked()) {
                size = client.get_response_body_length();
                data = PoolByteArray();
            }
        }
        PoolByteArray chunk = client.read_response_body_chunk();
        if (chunk.size() > 0) {
            data = data + chunk;
        }
        update_count += 1;
    }
};

struct CRequest {
    int h_request;
    String host;
    int port = 443;
    bool is_ssl = true;
    String url: String;
    Dictionary dict_request;
    UserCallback user_callback = nullptr;
    Array list_header;
    Array list_prologue_work;
    Array list_epilogue_work;
    CResult *o_result = nullptr;

    CRequest(int h_request) : h_request(h_request) { }
};


class PlayFabHTTPClient : HTTPClient {

    int _request_counter = 0;
    int _current_request = nullptr;
    Array _request_buffers;
    int status_curr = STATUS_DISCONNECTED;

    bool connecting = false;

    void pro_chk_entity_token() {
        assert(
            PlayFabSettings._internalSettings.EntityToken,
            "Must call EntityToken before calling this method"
        )
    }

    void pro_chk_secret_key() {
        assert(
            PlayFabSettings._internalSettings.DeveloperSecretKey,
            "Must have DeveloperSecretKey set to call this method"
        )
    }

    void pro_chk_session_ticket() {
        assert(
            PlayFabSettings._internalSettings.ClientSessionTicket,
            "Must be logged in to call this method"
        )
    }

    String pro_use_auth_authorization() {
        return "{0}: {1}".format(
            [
                "X-Authorization",
                PlayFabSettings._internalSettings.ClientSessionTicket
            ]
        )
    }

    String pro_use_auth_entity_token() {
        return "{0}: {1}".format(
            [
                "X-EntityToken",
                PlayFabSettings._internalSettings.EntityToken["EntityToken"]
            ]
        )
    }


    String pro_use_auth_secret_key() {
        return "{0}: {1}".format(
            [
                "X-SecretKey",
                PlayFabSettings._internalSettings.DeveloperSecretKey
            ]
        )
    }


    void pro_use_title_id(dict_request: Dictionary) {
        String K = "TitleId";

        if (K in dict_request) {
            return;
        } else {
            dict_request[K] = PlayFabSettings.TitleId;
        }

        assert(
            K in dict_request,
            "Must be have TitleId set to call this method"
        )
    }


    // -- epi --

    void epi_upd_attribute() {
        PlayFabSettings.AdvertisingIdType += "_Successful"
    }

    void epi_upd_entity_token(json_result) {
        String K = "EntityToken";
        Dictionary playFabResult = json_result.result["data"];

        if (K in playFabResult) {
            PlayFabSettings._internalSettings.EntityToken = playFabResult[K]
        }
    }


    void epi_upd_session_ticket(json_result) {
        String K = "SessionTicket";
        Dictionary playFabResult = json_result.result["data"];

        if (K in playFabResult) {
            PlayFabSettings._internalSettings.ClientSessionTicket = playFabResult[K]
        }
    }


    void epi_req_multi_step_client_login(json_result) {
        String K = "SettingsForUser";
        var playFabResult = json_result.result["data"];
        var settingsForUser = playFabResult[K];
        var disabledAds = PlayFabSettings.DisableAdvertising;
        var adIdType = PlayFabSettings.AdvertisingIdType;
        var adIdVal = PlayFabSettings.AdvertisingIdValue;

        if (settingsForUser && settingsForUser["NeedsAttribution"] && !disabledAds && adIdType && adIdVal) {
            Dictionary dict_request;
            if (adIdType == PlayFabSettings.AD_TYPE_IDFA) {
                dict_request["Idfa"] = adIdVal;
            } else if (adIdType == PlayFabSettings.AD_TYPE_ANDROID_ID) {
                dict_request["Adid"] = adIdVal;
            }
            PlayFab.Client.AttributeInstall(dict_request);
        }
    }


    Array build_host() {
        if (not PlayFabSettings.TitleId) {
            assert(false)
        }
        String host: String = ""
        int port = 443
        bool is_ssl = true
        if (!PlayFabSettings.ProductionEnvironmentURL.begins_with("http")) {
            if (PlayFabSettings.VerticalName) {
                host = PlayFabSettings.VerticalName;
            } else {
                host = PlayFabSettings.TitleId;
            }
        }

        host += PlayFabSettings.ProductionEnvironmentURL;

        return array(host, port, is_ssl);
    }


    void request_append( String url_path,
        Dictionary dict_request,
        UserCallback user_callback = nullptr,
        Dictionary dict_header_extra = Dictionary(),
        Array list_prologue_work = Array(),
        Array list_epilogue_work = Array()) {

        Array list_header = [
                "Content-Type: application/json",
                "X-PlayFabSDK: " + PlayFabSettings._internalSettings.SdkVersionString,
                "X-ReportErrorAsSuccess: true"
            ]

        if (PlayFabSettings._internalSettings.RequestGetParams) {
            int idx = 0;
            for (k in PlayFabSettings._internalSettings.RequestGetParams) {
                var v = PlayFabSettings._internalSettings.RequestGetParams[k]
                if (idx == 0) {
                    url_path += "?";
                    url_path += k;
                } else {
                    url_path += "&";
                    url_path += k;
                }
                url_path += "=";
                url_path += v;
                idx += 1;
            }
        }

        for (e_val in list_prologue_work) {
            match e_val:
                PlayFab.E_PRO.CHK_ENTITY_TOKEN:
                    pro_chk_entity_token()
                PlayFab.E_PRO.CHK_SECRET_KEY:
                    pro_chk_secret_key()
                PlayFab.E_PRO.CHK_SESSION_TICKET:
                    pro_chk_session_ticket()
                PlayFab.E_PRO.USE_AUTH_AUTHORIZATION:
                    list_header.append(pro_use_auth_authorization())
                PlayFab.E_PRO.USE_AUTH_ENTITY_TOKEN:
                    list_header.append(pro_use_auth_entity_token())
                PlayFab.E_PRO.USE_AUTH_SECRET_KEY:
                    list_header.append(pro_use_auth_secret_key())
                PlayFab.E_PRO.USE_TITLE_ID:
                    pro_use_title_id(dict_request)
        }

        for (k in dict_header_extra) {
            list_header.append("{0}: {1}".format([k, dict_header_extra[k]]))
        }

        _request_counter += 1;
        var o = CRequest.new(_request_counter);
        var list_host = build_host();

        o.host = list_host[0];
        o.port = list_host[1];
        o.is_ssl = list_host[2];
        o.url = url_path;
        o.dict_request = dict_request;
        o.user_callback = user_callback;
        o.list_header = list_header;
        o.list_prologue_work = list_prologue_work;
        o.list_epilogue_work = list_epilogue_work;

        _request_buffers.append(o);

        return o.h_request;
    }

    void dispatch(const CRequest &o_request) {

        String raw_text = o_request.o_result.data.get_string_from_utf8();
        var parse_result = JSON.parse(raw_text);

        if (parse_result.error == OK) {
            if (parse_result.result.has("code")) {
                if (parse_result.result["code"] == 200) {
                    for (e_val in o_request.list_epilogue_work) {
                        switch (e_val) {
                            PlayFab.E_EPI.REQ_MULTI_STEP_CLIENT_LOGIN:
                                epi_req_multi_step_client_login(parse_result)
                            PlayFab.E_EPI.UPD_ATTRIBUTE:
                                epi_upd_attribute()
                            PlayFab.E_EPI.UPD_ENTITY_TOKEN:
                                epi_upd_entity_token(parse_result)
                            PlayFab.E_EPI.UPD_SESSION_TICKET:
                                epi_upd_session_ticket(parse_result)
                        }
                    }
                }
            }
        }
        if (o_request.user_callback) {
            o_request.user_callback.call_func(
                o_request.h_request,
                o_request.o_result.response_code,
                o_request.o_result.dict_header,
                parse_result
            );
        }

        return nullptr;
    }

    void reset() {
        close();

        _current_request = nullptr;
        _request_buffers.clear();
        status_curr = get_status();

        connecting = false;
    }

    void request_cancel(const CRequest &h_request) {
        int pos = 0;
        Array list_remove_target;
        for (req in _request_buffers) {
            if (req.h_request == h_request) {
                list_remove_target.append(pos);
            }
            pos += 1;
        }
        for (n in list_remove_target) {
            _request_buffers.remove(n);
        }
        return list_remove_target.size() > 0;
    }

    void update(real_t delta) {
        if (poll() == OK) {
            return;
        }
        status_curr = get_status();

        if (_current_request == null) {
            if (_request_buffers.size() > 0) {
                _current_request = _request_buffers.pop_front();
                status_curr == STATUS_DISCONNECTED;
            } else {
                if (status_curr == STATUS_CONNECTION_ERROR) {
                    close();
                }
            }
        } else {
            if (status_curr == STATUS_DISCONNECTED) {
                if (connecting == false) {
                    var err = connect_to_host(
                        _current_request.host,
                        _current_request.port,
                        _current_request.is_ssl
                    )
                    if err == OK:
                        connecting = true
                    else:
                        assert(false)
                }
            } else if (status_curr == STATUS_CONNECTED) {
                connecting = false;

                if (!_current_request.o_result):
                    request(
                        HTTPClient.METHOD_POST,
                        _current_request.url,
                        _current_request.list_header,
                        JSON.print(_current_request.dict_request)
                    )
                    _current_request.o_result = CResult.new()

            } else if (status_curr == STATUS_BODY) {
                if has_response() {
                    _current_request.o_result.update(self);
                }
            }
            if (status_curr != STATUS_BODY) {
                if (_current_request.o_result != nullptr) {
                    if (_current_request.o_result.update_count > 0) {
                        _current_request = dispatch(_current_request);
                    }
                }
            }
        }
    }
};
