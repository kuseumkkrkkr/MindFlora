#include <iostream>
#include <vector>
#include <string>
#include <curl/curl.h>

using namespace std;

const string SERVER_URL = "https://blackwhite12.pythonanywhere.com";

size_t write_callback(void* contents, size_t size, size_t nmemb, string* output) {
    output->append((char*)contents, size * nmemb);
    return size * nmemb;
}

struct ParsedPacket {
    uint8_t sensor1 = 0;
    uint8_t sensor2 = 0;
    uint8_t sensor3 = 0;
    uint8_t sensor4 = 0;
    bool onoff = false;
    int led = 0;
};

void post_binary(const string& api_key, uint8_t sensor1, uint8_t sensor2, uint8_t sensor3, uint8_t sensor4) {
    CURL* curl = curl_easy_init();
    if (!curl) return;

    uint8_t sensor_data[4] = { sensor1, sensor2, sensor3, sensor4 };
    vector<uint8_t> post_data;

    post_data.insert(post_data.end(), api_key.begin(), api_key.end());
    post_data.push_back(0x01);
    post_data.push_back(4);
    post_data.insert(post_data.end(), sensor_data, sensor_data + 4);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");

    curl_easy_setopt(curl, CURLOPT_URL, (SERVER_URL + "/post_binary").c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, post_data.size());

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        cerr << "POST failed: " << curl_easy_strerror(res) << endl;
    else
        cout << "POST sent successfully.\n";

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

void onoff_bin(const string& api_key, bool onoff, int led) {
    CURL* curl;
    CURLcode res;
    struct curl_slist* headers = nullptr;

    curl = curl_easy_init();
    if (curl) {
        vector<uint8_t> post_data;

        post_data.insert(post_data.end(), api_key.begin(), api_key.end());
        post_data.push_back(0x02);
        post_data.push_back(1);
        post_data.push_back(onoff ? 0x01 : 0x00);

        headers = curl_slist_append(headers, "Content-Type: application/octet-stream");

        curl_easy_setopt(curl, CURLOPT_URL, (SERVER_URL + "/post_binary").c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, post_data.size());

        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            cerr << "OnOff POST failed: " << curl_easy_strerror(res) << endl;
        else
            cout << "OnOff POST sent successfully.\n";

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    curl = curl_easy_init();
    if (curl) {
        vector<uint8_t> post_data;
        headers = nullptr;

        if (led < 0) led = 0;
        if (led > 255) led = 255;

        post_data.insert(post_data.end(), api_key.begin(), api_key.end());
        post_data.push_back(0x03);
        post_data.push_back(1);
        post_data.push_back(static_cast<uint8_t>(led));

        headers = curl_slist_append(headers, "Content-Type: application/octet-stream");

        curl_easy_setopt(curl, CURLOPT_URL, (SERVER_URL + "/post_binary").c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, post_data.size());

        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            cerr << "LED POST failed: " << curl_easy_strerror(res) << endl;
        else
            cout << "LED POST sent successfully.\n";

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
}

ParsedPacket get_binary(const string& api_key) {
    ParsedPacket pkt;
    string response;

    CURL* curl = curl_easy_init();
    if (!curl) return pkt;

    string url = SERVER_URL + "/get_binary/" + api_key;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        cerr << "GET failed: " << curl_easy_strerror(res) << endl;
        curl_easy_cleanup(curl);
        return pkt;
    }

    size_t i = 0;
    while (i < response.size()) {
        string key = response.substr(i, 8); i += 8;
        uint8_t type = response[i++];
        uint8_t length = response[i++];

        if (type == 0x01 && length == 4) {
            pkt.sensor1 = response[i++];
            pkt.sensor2 = response[i++];
            pkt.sensor3 = response[i++];
            pkt.sensor4 = response[i++];
        }
        else if (type == 0x02 && length == 1) {
            pkt.onoff = response[i++];
        }
        else if (type == 0x03 && length == 1) {
            pkt.led = response[i++];
        }
        else {
            i += length;
        }
    }

    curl_easy_cleanup(curl);
    return pkt;
}

int main() {
    string api_key = "1C3BFB6C";
    post_binary(api_key, 10, 20, 30, 40);
    onoff_bin(api_key, true, 100);

    ParsedPacket pkt = get_binary(api_key);

    cout << "== 파싱된 결과 ==" << endl;
    cout << "센서1: " << (int)pkt.sensor1 << endl;
    cout << "센서2: " << (int)pkt.sensor2 << endl;
    cout << "센서3: " << (int)pkt.sensor3 << endl;
    cout << "센서4: " << (int)pkt.sensor4 << endl;
    cout << "OnOff: " << pkt.onoff << endl;
    cout << "LED: " << (int)pkt.led << endl;

    return 0;
}
