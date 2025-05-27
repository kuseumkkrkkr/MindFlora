#include <iostream>
#include <vector>
#include "sqlite3.h"
#include <curl/curl.h>
#include <ctime>
#include <nlohmann/json.hpp>
#include <string> 
#include "moonsoo.h" //센서데이터
#include <deque>
#include <numeric>
#include <locale>
#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;
using namespace std;



//수정금지 필수함수
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

/*
██╗   ██╗ ██████╗  ██████╗ ███╗   ██╗     █████╗  ██████╗  █████╗ ██╗███╗   ██╗
╚██╗ ██╔╝██╔═══██╗██╔═══██╗████╗  ██║    ██╔══██╗██╔════╝ ██╔══██╗██║████╗  ██║
 ╚████╔╝ ██║   ██║██║   ██║██╔██╗ ██║    ███████║██║  ███╗███████║██║██╔██╗ ██║
  ╚██╔╝  ██║   ██║██║   ██║██║╚██╗██║    ██╔══██║██║   ██║██╔══██║██║██║╚██╗██║
   ██║   ╚██████╔╝╚██████╔╝██║ ╚████║    ██║  ██║╚██████╔╝██║  ██║██║██║ ╚████║
   ╚═╝    ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝    ╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═╝╚═╝╚═╝  ╚═══╝
*/

// 전방 선언
struct Answer;
class ChatCache;

//응답 기록 데이터용 구조체&함수 ==============================================
struct Answer {
    //응답 분석 구조체 (이 형식대로 구조체에 들어가고, 이 외 형식은 강제로 제거)

    //시스템정보
    int id = -1; //몇번째인가
    string chat_id; //어느 세션인가
    string role; //System, User 

    //응답정보
    string text; //응답 텍스트
    string key1; //키워드 세개
    string key2;
    string key3;
    float approval = 0.0;
    time_t timestamp = time(nullptr);
};

//JS 직렬화 함수 (구조체 -> JSON), 밑에는 그 반대                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           
inline void to_json(json& j, const Answer& msg) { //구조체 그대로 다시 주기
    j = json{
        {"id", msg.id},
        {"chat_id", msg.chat_id},
        {"role", msg.role},
        {"content", msg.text},
        {"timestamp", msg.timestamp},
        {"key1", msg.key1},
        {"key2", msg.key2},
        {"key3", msg.key3}
    };
}

inline void from_json(const json& j, Answer& msg) {
    j.at("id").get_to(msg.id);
    j.at("chat_id").get_to(msg.chat_id);
    j.at("role").get_to(msg.role);
    j.at("content").get_to(msg.text);
    j.at("timestamp").get_to(msg.timestamp);
    j.at("key1").get_to(msg.key1);
    j.at("key2").get_to(msg.key2);
    j.at("key3").get_to(msg.key3);
}

//텍스트 분석용 구조체&함수 ==================================================
struct TextAnalysisResult {
    vector<string> keywords;
    string emotion_label;
    double impression = 0.0;
    double valence = 0.0;
    double arousal = 0.0;
    double impressiveness = 0.0;
};

inline void to_json(json& j, const TextAnalysisResult& result) {
    j = json{
        {"keywords", result.keywords},
        {"emotion_label", result.emotion_label},
        {"impression", result.impression},
        {"valence", result.valence},
        {"arousal", result.arousal},
        {"impressiveness", result.impressiveness}
    };
}

inline void from_json(const json& j, TextAnalysisResult& result) {
    j.at("keywords").get_to(result.keywords);
    j.at("emotion_label").get_to(result.emotion_label);
    j.at("impression").get_to(result.impression);
    j.at("valence").get_to(result.valence);
    j.at("arousal").get_to(result.arousal);
    j.at("impressiveness").get_to(result.impressiveness);
}

//데이터베이스 연결 및 초기화 함수 ==================================================
// 데이터베이스 초기화 및 테이블 생성
sqlite3* db = nullptr;

bool InitializeDB() {
    int rc = sqlite3_open("memory.db", &db);
    if (rc) {
        cerr << "데이터베이스 열기 실패: " << sqlite3_errmsg(db) << endl;
        return false;
    }

    // NonSector 테이블 생성
    const char* sql_nonsector =
        "CREATE TABLE IF NOT EXISTS nonsector ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "chat_id TEXT,"
        "role TEXT,"
        "text TEXT,"
        "key1 TEXT,"
        "key2 TEXT,"
        "key3 TEXT,"
        "approval REAL,"
        "timestamp INTEGER"
        ");";

    // Sector 테이블 생성
    const char* sql_sector =
        "CREATE TABLE IF NOT EXISTS sector ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "chat_id TEXT,"
        "role TEXT,"
        "text TEXT,"
        "key1 TEXT,"
        "key2 TEXT,"
        "key3 TEXT,"
        "approval REAL,"
        "timestamp INTEGER"
        ");";

    // Space 테이블 생성
    const char* sql_space =
        "CREATE TABLE IF NOT EXISTS space ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "summary TEXT,"
        "timestamp INTEGER"
        ");";

    // Endless Memory 테이블 생성
    const char* sql_endless =
        "CREATE TABLE IF NOT EXISTS endless ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "chat_id TEXT,"
        "role TEXT,"
        "text TEXT,"
        "key1 TEXT,"
        "key2 TEXT,"
        "key3 TEXT,"
        "approval REAL,"
        "timestamp INTEGER"
        ");";

    char* errMsg = nullptr;
    if (sqlite3_exec(db, sql_nonsector, nullptr, nullptr, &errMsg) != SQLITE_OK ||
        sqlite3_exec(db, sql_sector, nullptr, nullptr, &errMsg) != SQLITE_OK ||
        sqlite3_exec(db, sql_space, nullptr, nullptr, &errMsg) != SQLITE_OK ||
        sqlite3_exec(db, sql_endless, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        cerr << "테이블 생성 오류: " << errMsg << endl;
        sqlite3_free(errMsg);
        return false;
    }

    return true;
}

// Answer 구조체를 DB에 저장하는 함수
bool SaveAnswerToDB(const Answer& answer, const std::string& table_name) {
    string sql = "INSERT INTO " + table_name +
        " (chat_id, role, text, key1, key2, key3, approval, timestamp) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        cerr << "SQL 준비 오류: " << sqlite3_errmsg(db) << endl;
        return false;
    }

    sqlite3_bind_text(stmt, 1, answer.chat_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, answer.role.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, answer.text.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, answer.key1.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, answer.key2.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, answer.key3.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 7, answer.approval);
    sqlite3_bind_int64(stmt, 8, answer.timestamp);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

// Space 요약을 DB에 저장하는 함수
bool SaveSpaceToDB(const string& summary) {
    string sql = "INSERT INTO space (summary, timestamp) VALUES (?, ?);";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        cerr << "SQL 준비 오류: " << sqlite3_errmsg(db) << endl;
        return false;
    }

    sqlite3_bind_text(stmt, 1, summary.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, time(nullptr));

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

//ChatGPT API 호출 함수 =========================================================
// GPT 호출 메인함수 
json gpt(const string& prompt, const string& api_key) {
    CURL* curl = curl_easy_init();
    string response;
    json result;

    if (curl) {
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json; charset=UTF-8");
        headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key).c_str());

        // API 요청 본문 구성
        json request_body = {
            {"model", "gpt-4.1"},
            {"messages", json::array({
                {
                    {"role", "user"},
                    {"content", prompt}
                }
            })},
            {"temperature", 0.7}
        };

        string request_body_str = request_body.dump(-1, ' ', false, json::error_handler_t::replace);

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/chat/completions");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_ENCODING, "UTF-8");

        CURLcode res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
            result["error"] = curl_easy_strerror(res);
        }
        else {
            try {
                result = json::parse(response); // 기본적으로 std::string을 처리합니다.
            } catch (const json::parse_error& e) {
                cerr << "JSON 파싱 오류: " << e.what() << endl;
                // 오류 처리 로직 추가
            }
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    else {
        result["error"] = "CURL 초기화 실패";
    }

    return result;
}

//텍스트 감정 분석함수부분=======================================================
// 분석용 ChatGPT API 호출 함수
json CallChatGPTForAnalysis(const string& prompt, const string& api_key) {
    json response = gpt(prompt, api_key);

    if (response.contains("error")) {
        throw runtime_error(response["error"].get<string>());
    }

    try {
        string content = response["choices"][0]["message"]["content"];
        return json::parse(content);
    }
    catch (const json::exception& e) {
        throw runtime_error("API 응답 파싱 오류: " + string(e.what()));
    }
}

// 텍스트 분석 통합 함수 (원클릭)
TextAnalysisResult TextAnalysis(const string& text, string api_key) {
    TextAnalysisResult result;
    Answer answer;
    
    try {
        if (api_key.empty()) {
            throw runtime_error("API 키 오류");
        }

        // UTF-8 검증
        if (!text.empty()) {
            // 입력 문자열을 UTF-8로 변환
            string utf8_text = text;
            
            // 통합된 분석 프롬프트
            string unified_prompt =
                "다음 텍스트를 분석하여 정확히 아래 JSON 형식으로 반환해주세요.\n"
                "{\n"
                "  \"keywords\": [\"키워드1\", \"키워드2\", \"키워드3\"],\n"
                "  \"emotion_label\": \"감정라벨\",\n"
                "  \"valence\": 0.0,\n"
                "  \"arousal\": 0.0,\n"
                "  \"impressiveness\": 0.0\n"
                "}\n"
                "조건:\n"
                "- keywords: 정확히 3개의 핵심 키워드 추출\n"
                "- emotion_label: 감정을 나타내는 한 단어\n"
                "- valence: 0 ~ 2 사이의 실수\n"
                "- arousal: 0 ~ 1 사이의 실수\n"
                "- impressiveness: 0 ~ 1 사이의 실수\n"
                "분석할 텍스트: " + utf8_text;

            // 나머지 코드는 동일
            json analysis_response = CallChatGPTForAnalysis(unified_prompt, api_key);
            result.keywords = analysis_response["keywords"].get<vector<string>>();
            result.emotion_label = analysis_response["emotion_label"].get<string>();
            result.valence = analysis_response["valence"].get<double>();
            result.arousal = analysis_response["arousal"].get<double>();
            result.impressiveness = analysis_response["impressiveness"].get<double>();

            // 기존 인상 점수 계산 로직 유지
            double norm_valence = (result.valence + 1.0) / 2.0;
            result.impression = (norm_valence * 0.1 + result.arousal * 0.4 +
                result.impressiveness * 0.5) * 10.0;

            // Answer 구조체 통합
            if (!result.keywords.empty()) {
                answer.key1 = result.keywords[0];
                if (result.keywords.size() > 1) answer.key2 = result.keywords[1];
                if (result.keywords.size() > 2) answer.key3 = result.keywords[2];
            }
            answer.approval = static_cast<float>(result.impression) / 10.0f;
        }
    }
    catch (const exception& e) {
        cerr << "텍스트 분석 오류: " << e.what() << endl;
        // 오류 시 기본값 설정
        result.keywords = vector<string>();
        result.emotion_label = "";
        result.impression = 0.0;
        result.valence = 0.0;
        result.arousal = 0.0;
        result.impressiveness = 0.0;
    }

    return result;
}

//기억 생성부========================================================================
float avg_aproval = 0.0; //4
float avg_aproval_20 = 0.0; //20

struct Memory {
    deque<Answer> nonSector;     // 최근 40개 저장
    deque<Answer> sector;        // 중요 응답 5개 저장
    deque<Answer> endless;       // 영구 기억 저장 추가
    deque<string> space;         // 요약된 내용 3개 저장
    static const size_t NON_SECTOR_MAX = 40;
    static const size_t SECTOR_MAX = 7;
    static const size_t SPACE_MAX = 3;
    static const size_t ENDLESS_MAX = 10;  // 영구 기억 최대 개수
};

// 캐시 시스템 구현
class ChatCache {
private:
    // 싱글톤 인스턴스
    static ChatCache* instance;

    // 캐시 초기화 상태
    bool isInitialized = false;

    ChatCache() {} // private 생성자

public:
    Memory memory;  // 기존 메모리 구조체

    static ChatCache* getInstance() {
        if (instance == nullptr) {
            instance = new ChatCache();
        }
        return instance;
    }

    // DB에서 데이터 로드하여 캐시 초기화
    bool InitializeCache() {
        if (isInitialized) return true;

        try {
            // NonSector 로드 (최근 40개)
            string sql_nonsector = "SELECT * FROM nonsector ORDER BY timestamp DESC LIMIT 40";
            LoadAnswersFromDB(sql_nonsector, memory.nonSector);

            // Sector 로드 (최근 7개)
            string sql_sector = "SELECT * FROM sector ORDER BY timestamp DESC LIMIT 7";
            LoadAnswersFromDB(sql_sector, memory.sector);

            // Space 로드 (최근 3개)
            string sql_space = "SELECT summary FROM space ORDER BY timestamp DESC LIMIT 3";
            LoadSpaceFromDB(sql_space, memory.space);

            // Endless 메모리 로드 추가
            string sql_endless = "SELECT * FROM endless ORDER BY timestamp DESC LIMIT 10";
            LoadAnswersFromDB(sql_endless, memory.endless);

            // 평균 계산 초기화
            if (!memory.nonSector.empty()) {
                for (const auto& answer : memory.nonSector) {
                    CalculateApprovals(answer);
                }
            }

            isInitialized = true;
            return true;
        }
        catch (const exception& e) {
            cerr << "캐시 초기화 오류: " << e.what() << endl;
            return false;
        }
    }

    // DB에서 Answer 데이터 로드 - 선언과 정의를 합침
    void LoadAnswersFromDB(const string& sql, deque<Answer>& target) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            throw runtime_error(string("SQL 준비 오류: ") + sqlite3_errmsg(db));
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Answer answer;
            answer.id = sqlite3_column_int(stmt, 0);
            answer.chat_id = string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
            answer.role = string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
            answer.text = string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
            answer.key1 = string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
            answer.key2 = string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
            answer.key3 = string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));
            answer.approval = static_cast<float>(sqlite3_column_double(stmt, 7));
            answer.timestamp = sqlite3_column_int64(stmt, 8);

            target.push_back(answer);
        }

        sqlite3_finalize(stmt);
    }

    // DB에서 Space 데이터 로드
    void LoadSpaceFromDB(const string& sql, deque<string>& target) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            throw runtime_error("SQL 준비 오류");
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            string summary = string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
            target.push_back(summary);
        }

        sqlite3_finalize(stmt);
    }

    // 캐시 정리
    void ClearCache() {
        memory.nonSector.clear();
        memory.sector.clear();
        memory.space.clear();
        memory.endless.clear();  // endless 메모리도 클리어
        isInitialized = false;
    }

    // 평균 계산 함수를 클래스 내부로 이동
    void CalculateApprovals(const Answer& newAnswer) {
        static deque<float> recent4;  // 최근 4개 저장
        static deque<float> recent20; // 최근 20개 저장

        // 최근 4개 관리
        recent4.push_back(newAnswer.approval);
        if (recent4.size() > 4) {
            recent4.pop_front();
        }

        // 최근 20개 관리
        recent20.push_back(newAnswer.approval);
        if (recent20.size() > 20) {
            recent20.pop_front();
        }

        // 평균 계산
        avg_aproval = accumulate(recent4.begin(), recent4.end(), 0.0f) / recent4.size();
        avg_aproval_20 = accumulate(recent20.begin(), recent20.end(), 0.0f) / recent20.size();
    }
};

// 싱글톤 인스턴스 초기화
ChatCache* ChatCache::instance = nullptr;

Memory memory; //전역변수로 가져오기

void CalculateApprovals(const Answer& newAnswer) {
    ChatCache* cache = ChatCache::getInstance();
    cache->CalculateApprovals(newAnswer);
}

// 전방 선언
void Space(const string& combinedText, string api_key);

// 최근40개
void NonSector(const Answer& newAnswer) {
    ChatCache* cache = ChatCache::getInstance();

    // 메모리에 추가
    cache->memory.nonSector.push_back(newAnswer);

    if (cache->memory.nonSector.size() > Memory::NON_SECTOR_MAX) {
        cache->memory.nonSector.pop_front();
    }

    // 평균 계산 업데이트
    CalculateApprovals(newAnswer);

    // 실시간 DB 저장 유지
    SaveAnswerToDB(newAnswer, "nonsector");
}

void Sector(const Answer& answer, string api_key) {
    ChatCache* cache = ChatCache::getInstance();

    // approval이 평균보다 3 이상 높은 경우에만 저장
    if (answer.approval >= (avg_aproval + 3.0f)) {
        cache->memory.sector.push_back(answer);
        SaveAnswerToDB(answer, "sector"); // DB 저장 추가

        // 섹터가 가득 찼을 때 (7개)
        if (cache->memory.sector.size() >= Memory::SECTOR_MAX) {
            // 가장 오래된 응답들을 하나의 문자열로 결합
            string combinedText;
            for (int i = 0; i < 3; ++i) {  // 처음 3개 응답 결합
                combinedText += cache->memory.sector.front().text + "\n";
                cache->memory.sector.pop_front();
            }

            // Space로 요약하여 저장
            Space(combinedText, api_key);
        }
    }
}

void Space(const string& combinedText, string api_key) {
    ChatCache* cache = ChatCache::getInstance();

    try {
        // GPT를 사용하여 텍스트 요약
        string summary_prompt =
            "다음 대화들을 한 문장으로 간단히 요약해주세요:\n" + combinedText;

        json response = gpt(summary_prompt, api_key);
        string summary = response["choices"][0]["message"]["content"];

        // 요약된 내용 저장
        cache->memory.space.push_back(summary);
        SaveSpaceToDB(summary); // DB 저장 추가

        // 최대 3개까지만 유지
        if (cache->memory.space.size() > Memory::SPACE_MAX) {
            cache->memory.space.pop_front();
        }
    }
    catch (const exception& e) {
        cerr << "요약 생성 오류: " << e.what() << endl;
    }
}

void EndlessMemory(const Answer& answer) {
    // approval이 9 이상이거나 최근 20개 평균보다 3 이상 클 때
    if (answer.approval >= 9.0f || (answer.approval >= (avg_aproval_20 + 3.0f))) {
        SaveAnswerToDB(answer, "endless"); // DB 저장 추가
    }
}

// 대화 시작 시 호출할 함수
bool StartChat() {
    ChatCache* cache = ChatCache::getInstance();
    return cache->InitializeCache();
}

// 대화 종료 시 호출할 함수
void EndChat() {
    ChatCache* cache = ChatCache::getInstance();
    cache->ClearCache();
}

//프롬포트 빌딩계열 함수 =================================================================
string PromptBuilder(string api, string plant_name) {
    ChatCache* cache = ChatCache::getInstance();
    string base_prompt = "";

    // 1. 기본 페르소나 프롬프트
	base_prompt = "너의 이름은" + plant_name + "당신은 지금 친한 친구와 대화하고 있다. 200자 이하로 감성적으로 답장하라";

    // 2. 기억 통합 - 디버깅 정보 추가
    cout << "[DEBUG] NonSector 개수: " << cache->memory.nonSector.size() << endl;
    cout << "[DEBUG] Sector 개수: " << cache->memory.sector.size() << endl;
    cout << "[DEBUG] Space 개수: " << cache->memory.space.size() << endl;

    // NonSector (최근 기억) - 개선된 순회
    if (!cache->memory.nonSector.empty()) {
        base_prompt += "\n\n[직전대화]";
        int count = 0;
        // 최신 것부터 보여주기 위해 역순으로 순회
        for (auto it = cache->memory.nonSector.rbegin();
            it != cache->memory.nonSector.rend() && count < 5;
            ++it, ++count) {
            base_prompt += "\n- [" + it->role + "] " + it->text;
            // 키워드 정보도 추가
            if (!it->key1.empty()) {
                base_prompt += " (키워드: " + it->key1;
                if (!it->key2.empty()) base_prompt += ", " + it->key2;
                if (!it->key3.empty()) base_prompt += ", " + it->key3;
                base_prompt += ")";
            }
        }
    }

    // Sector (중요 기억) - 개선
    if (!cache->memory.sector.empty()) {
        base_prompt += "\n\n[단기 중점기억]";
        for (const auto& ans : cache->memory.sector) {
            base_prompt += "\n- [중요] " + ans.text;
            base_prompt += " (인상점수: " + to_string(ans.approval) + ")";
        }
    }

    // Space (요약된 기억) - 개선
    if (!cache->memory.space.empty()) {
        base_prompt += "\n\n[장기 열화기억]";
        for (const auto& summary : cache->memory.space) {
            base_prompt += "\n- [요약] " + summary;
        }
    }

    // Endless Memory 섹션 추가
    if (!cache->memory.endless.empty()) {
        base_prompt += "\n\n[장기기억]";
        for (const auto& ans : cache->memory.endless) {
            base_prompt += "\n- [영구] " + ans.text;
            
            // 키워드 정보 추가
            if (!ans.key1.empty()) {
                base_prompt += " (키워드: " + ans.key1;
                if (!ans.key2.empty()) base_prompt += ", " + ans.key2;
                if (!ans.key3.empty()) base_prompt += ", " + ans.key3;
                base_prompt += ")";
            }
            
            // 감정 강도 표시
            base_prompt += " [감정 강도: " + to_string(ans.approval) + "]";
        }
    }

    // 디버깅 정보 추가
    cout << "[DEBUG] Endless Memory 개수: " << cache->memory.endless.size() << endl;

    // 3. 현재 상태 프롬프트 추가 - 에러 처리 개선
    base_prompt += "\n\n현재 상태:";

    try {
        // 센서값 해석 및 상태 추가
        auto sensorData = get_binary(api);

        // 센서 데이터 유효성 검사
        cout << "[DEBUG] 센서1 값: " << (int)sensorData.sensor1 << endl;
        cout << "[DEBUG] 센서2 값: " << (int)sensorData.sensor2 << endl;

        // 온도 (sensor1: 0-50℃를 0-255로 매핑)
        float temp = (sensorData.sensor1 / 255.0f) * 50.0f;
        base_prompt += "\n- 현재 온도: " + to_string(temp) + "℃";

        if (temp <= 15.0f) {
            base_prompt += " → 차가운 공기에 움츠러들고 있어요";
        }
        else if (temp > 28.0f) {
            base_prompt += " → 더위 때문에 힘이 빠져요";
        }
        else {
            base_prompt += " → 적당한 온도로 기분이 좋아요";
        }

        // 습도 (sensor2: 0-100%를 0-255로 매핑)
        float humidity = (sensorData.sensor2 / 255.0f) * 100.0f;
        base_prompt += "\n- 현재 습도: " + to_string(humidity) + "%";

        if (humidity < 35.0f) {
            base_prompt += " → 공기가 너무 건조해서 목이 마르고 잎이 바스락거려요";
        }
        else if (humidity > 65.0f) {
            base_prompt += " → 습도가 너무 높아서 숨쉬기가 답답해요";
        }
        else {
            base_prompt += " → 습도가 적당해서 상쾌해요";
        }

        // 감정 상태 추가 (평균 승인도 기반)
        base_prompt += "\n- 최근 기분: ";
        if (avg_aproval >= 7.0f) {
            base_prompt += "매우 행복해요!";
        }
        else if (avg_aproval >= 5.0f) {
            base_prompt += "기분이 좋아요";
        }
        else if (avg_aproval >= 3.0f) {
            base_prompt += "평범해요";
        }
        else {
            base_prompt += "조금 우울해요";
        }

    }
    catch (const exception& e) {
        cout << "[ERROR] 센서 데이터 읽기 실패: " << e.what() << endl;
        base_prompt += "\n- 센서 상태를 확인할 수 없어서 조금 불안해요";
    }

    // 4. 디버깅용 프롬프트 출력
    cout << "\n[DEBUG] 생성된 프롬프트 길이: " << base_prompt.length() << endl;
    cout << "[DEBUG] 평균 승인도(4개): " << avg_aproval << endl;
    cout << "[DEBUG] 평균 승인도(20개): " << avg_aproval_20 << endl;

    return base_prompt;
}

int main(void) {
    // 1. 콘솔 입출력 UTF-8 설정
#ifdef _WIN32
    // Windows에서 UTF-8 입출력을 위한 설정
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // 로케일 설정
    setlocale(LC_ALL, ".UTF-8");
    
    // 이전 코드와 동일
    string api = "";
    string api_key = "";
	string plant_name = "김문수";

    if (!InitializeDB()) {
        cerr << "데이터베이스 초기화 실패" << endl;
        return 1;
    }

    // 2. 대화 세션 시작 및 캐시 초기화
    if (!StartChat()) {
        cerr << "채팅 세션 초기화 실패" << endl;
        return 1;
    }

    cout << "세션 통과" << endl;

    try {
        // 3. 채팅 세션 ID 생성
        string chat_id = to_string(time(nullptr));
        
        while (true) {
            // 4. 프롬프트 생성 및 사용자 입력 처리
            string userInput;
            cout << "\n사용자 입력: ";
            getline(cin, userInput);

            if (userInput == "quit" || userInput == "exit") {
                break;
            }

            // 5. 사용자 입력 분석 및 저장
            Answer userAnswer;
            userAnswer.chat_id = chat_id;
            userAnswer.role = "user";
            userAnswer.text = userInput;
            userAnswer.timestamp = time(nullptr);

            // 텍스트 분석 수행
            TextAnalysisResult analysis = TextAnalysis(userInput, api_key);
            
            // 분석 결과를 Answer에 통합
            if (!analysis.keywords.empty()) {
                userAnswer.key1 = analysis.keywords[0];
                if (analysis.keywords.size() > 1) userAnswer.key2 = analysis.keywords[1];
                if (analysis.keywords.size() > 2) userAnswer.key3 = analysis.keywords[2];
            }
            userAnswer.approval = static_cast<float>(analysis.impression) / 10.0f;

            // 6. 메모리 시스템에 저장
            NonSector(userAnswer);
            if (userAnswer.approval >= avg_aproval + 3.0f) {
                Sector(userAnswer, api);
            }
            if (userAnswer.approval >= 9.0f || userAnswer.approval >= avg_aproval_20 + 3.0f) {
                EndlessMemory(userAnswer);
            }

            // 7. AI 응답 생성
            string prompt = PromptBuilder(api, plant_name);
            json response = gpt(prompt, api_key);
            
            if (!response.contains("error")) {
                string ai_response = response["choices"][0]["message"]["content"];
                cout << ai_response << endl;

                // 8. AI 응답 처리 및 저장
                Answer aiAnswer;
                aiAnswer.chat_id = chat_id;
                aiAnswer.role = "assistant";
                aiAnswer.text = ai_response;
                aiAnswer.timestamp = time(nullptr);

                // AI 응답 분석
                TextAnalysisResult ai_analysis = TextAnalysis(ai_response, api_key);
                if (!ai_analysis.keywords.empty()) {
                    aiAnswer.key1 = ai_analysis.keywords[0];
                    if (ai_analysis.keywords.size() > 1) aiAnswer.key2 = ai_analysis.keywords[1];
                    if (ai_analysis.keywords.size() > 2) aiAnswer.key3 = ai_analysis.keywords[2];
                }
                aiAnswer.approval = static_cast<float>(ai_analysis.impression) / 10.0f;

                // AI 응답 메모리 저장
                NonSector(aiAnswer);
                if (aiAnswer.approval >= avg_aproval + 3.0f) {
                    Sector(aiAnswer, api_key);
                }
                if (aiAnswer.approval >= 9.0f || aiAnswer.approval >= avg_aproval_20 + 3.0f) {
                    EndlessMemory(aiAnswer);
                }
            } else {
                cout << "\n오류 발생: " << response["error"] << endl;
            }
        }
    }
    catch (const exception& e) {
        cerr << "실행 중 오류 발생: " << e.what() << endl;
    }

    // 9. 정리 작업
    EndChat();
    sqlite3_close(db);
    return 0;
}









