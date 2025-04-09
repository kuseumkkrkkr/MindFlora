import json
import sqlite3
import datetime
import statistics
from flask import Flask, render_template, request, jsonify
from flask_socketio import SocketIO, emit
from google import genai
import re

# ---------------------------
# 초기 설정
# ---------------------------
client = genai.Client(api_key="API_KEY")
app = Flask(__name__)
app.config['SECRET_KEY'] = 'secret!'
socketio = SocketIO(app, cors_allowed_origins="*")
DB_FILE = "memory.db"

# ---------------------------
# DBMS
# ---------------------------
def init_db():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('''
        CREATE TABLE IF NOT EXISTS long_term_memory (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            keyword1 TEXT,
            keyword2 TEXT,
            keyword3 TEXT,
            impression REAL,
            text TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    c.execute('''
        CREATE TABLE IF NOT EXISTS short_term_memory (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            sector INTEGER,
            summary TEXT,
            high_impression_text TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    c.execute('''
        CREATE TABLE IF NOT EXISTS chat_accumulation (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            sector INTEGER,
            summary TEXT,
            mean_impression REAL,
            std_impression REAL,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    conn.commit()
    conn.close()

# ---------------------------
# 유틸 함수
# ---------------------------
def clean_and_parse_json(text):
    try:
        text = re.sub(r"```json", "", text)
        text = re.sub(r"```", "", text).strip()
        text = text.replace("'", '"')
        return json.loads(text)
    except Exception as e:
        print("JSON 파싱 오류:", e)
        return {"keywords": [], "impression": 0.0, "emotion_label": "", "valence": 0.0, "arousal": 0.0, "impressiveness": 0.0}

# ---------------------------
# AI 활용 함수
# ---------------------------
def extract_keywords_and_impression(text): #키워드 익스트레킹. 감정 키워드를 분석하여 넘겨줌 ==========================
    # 이 부분에서 실제로 플루치크 감정바퀴 구현시도 예정정
    prompt = f"""다음 텍스트에서 핵심 키워드 3개와 전반적인 인상(0~10 사이의 점수)을 JSON 형식으로 반환해줘.
예시:
{{
  "keywords": ["키워드1", "키워드2", "키워드3"],
  "impression": 7.5
}}
텍스트: {text}"""
    try:
        response = client.models.generate_content(model="gemini-2.0-flash", contents=prompt)
        content = response.text.strip()
        print("AI 응답 (키워드/인상):", content)
        data = clean_and_parse_json(content)
        return data.get("keywords", []), data.get("impression", 0.0)
    except Exception as e:
        print("키워드 추출 오류:", e)
        return [], 0.0

def analyze_emotion(text): #감정 분석 프롬포트
    prompt = f"""다음 텍스트의 감정을 분석해줘. 결과는 아래 JSON 형식으로 반환해줘.
예시:
{{
  "emotion_label": "짜증",
  "valence": 0.2,
  "arousal": 0.9,
  "impressiveness": 0.82
}}

valence : 0 ~ 2의 실수 (소숫점 0.00)
텍스트: {text}"""
    try:
        response = client.models.generate_content(model="gemini-2.0-flash", contents=prompt)
        content = response.text.strip()
        print("AI 응답 (감정 분석):", content)
        data = clean_and_parse_json(content)
        return {
            "emotion_label": data.get("emotion_label", ""),
            "valence": data.get("valence", 0.0),
            "arousal": data.get("arousal", 0.0),
            "impressiveness": data.get("impressiveness", 0.0)
        }
    except Exception as e:
        print("감정 분석 오류:", e)
        return {"emotion_label": "", "valence": 0.0, "arousal": 0.0, "impressiveness": 0.0}

def summarize_text(text): #주기적 텍스트 요약 함수 , 호출사항 없음음
    prompt = f"다음 대화 내용을 간결하게 요약해줘:\n\n{text}"
    try:
        response = client.models.generate_content(model="gemini-2.0-flash", contents=prompt)
        return response.text.strip()
    except Exception as e:
        print("요약 오류:", e)
        return text


#감정 분석 로직함수 ===================
#실제로 이 부분에 주목해야 함
#감정의 긍부정 = Val, 표현의 깊이 = Aro, 표현의 강도 = Imp
#=====================================

def compute_impression(emotion): #감정 분석함수
    norm_valence = (emotion.get("valence", 0.0) + 1) / 2 #밸런싱 가중치부문문
    arousal = emotion.get("arousal", 0.0)
    impressiveness = emotion.get("impressiveness", 0.0)
    impression = (norm_valence * 0.1 + arousal * 0.4 + impressiveness * 0.5) * 10
    #impression = (norm_valence * 0.3 + arousal * 0.3 + impressiveness * 0.4) * 10 기존 코드, 소수점 도합 무조건 1이 되어야함함
    return impression


# ---------------------------
# 메모리 저장 함수 (미완성성)
# ---------------------------
IMPRESSION_THRESHOLD = 7.0 #인상도가 7이 넘으면 저장장

def store_long_term_memory(keywords, impression, text): #키워드, 감정, 텍스트 저장 장기기억부문문
    if impression < IMPRESSION_THRESHOLD or len(keywords) < 3:
        return
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('''
        INSERT INTO long_term_memory (keyword1, keyword2, keyword3, impression, text)
        VALUES (?, ?, ?, ?, ?)
    ''', (keywords[0], keywords[1], keywords[2], impression, text))
    conn.commit()
    conn.close()
#===============================================================

def store_short_term_memory(sector, summary, high_impression_text): #압축된 단기기억 섹터터
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('''
        INSERT INTO short_term_memory (sector, summary, high_impression_text)
        VALUES (?, ?, ?)
    ''', (sector, summary, high_impression_text))
    conn.commit()
    conn.close()

def store_chat_accumulation(sector, texts, impressions): #숏 섹터 (현재 호출사항이 지정이 안되어있음음)
    if not impressions:
        return
    mean_impression = statistics.mean(impressions)
    std_impression = statistics.stdev(impressions) if len(impressions) > 1 else 0.0
    summary = summarize_text("\n".join(texts))
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('''
        INSERT INTO chat_accumulation (sector, summary, mean_impression, std_impression)
        VALUES (?, ?, ?, ?)
    ''', (sector, summary, mean_impression, std_impression))
    conn.commit()
    conn.close()

# ---------------------------
# 대화 처리 함수 (디버깅 레이어어)
# ---------------------------
def process_conversation(conversation_text, sector=1):
    keywords, base_impression = extract_keywords_and_impression(conversation_text)
    print("추출된 키워드:", keywords, "기본 인상도:", base_impression)
    emotion = analyze_emotion(conversation_text)
    computed_impression = compute_impression(emotion)
    print("감정 분석 결과:", emotion, "계산된 인상도:", computed_impression)

    store_long_term_memory(keywords, computed_impression, conversation_text)
    summary = summarize_text(conversation_text)
    store_short_term_memory(sector, summary, conversation_text)
    store_chat_accumulation(sector, [conversation_text], [computed_impression])

    return {
         "status": "success",
         "message": conversation_text,
         "impression": computed_impression,
         "keywords": keywords,
         "emotion": emotion
    }

# ---------------------------
# Flask 라우트
# ---------------------------
@app.route('/')
def index():
    return '''
    <!doctype html>
    <html>
      <head>
        <title>상시 텍스트 채팅</title>
        <script src="https://cdnjs.cloudflare.com/ajax/libs/socket.io/4.4.1/socket.io.min.js"></script>
      </head>
      <body>
        <h1>상시 텍스트 채팅</h1>
        <ul id="messages"></ul>
        <form id="chatForm">
          <input id="message" autocomplete="off" /><button>전송</button>
        </form>
        <script>
          var socket = io();
          var form = document.getElementById('chatForm');
          var input = document.getElementById('message');
          var messages = document.getElementById('messages');
          form.addEventListener('submit', function(e) {
            e.preventDefault();
            if (input.value) {
              socket.emit('chat_message', input.value);
              input.value = '';
            }
          });
          socket.on('chat_response', function(data) {
            var item = document.createElement('li');
            item.textContent = "사용자: " + data.message + " (인상도: " + data.impression.toFixed(2) + ", 키워드: " + data.keywords.join(", ") + ", 감정: " + data.emotion.emotion_label + ")";
            messages.appendChild(item);
            window.scrollTo(0, document.body.scrollHeight);
          });
        </script>
      </body>
    </html>
    '''

# ---------------------------
# SocketIO 이벤트
# ---------------------------
@socketio.on('chat_message')
def handle_chat_message(msg):
    print("받은 메시지:", msg)
    sector = 1
    result = process_conversation(msg, sector)
    emit('chat_response', result, broadcast=True)

# ---------------------------
# 메인 실행
# ---------------------------
if __name__ == '__main__':
    init_db()
    print("채팅 서버 시작 중...")
    socketio.run(app, host='0.0.0.0', port=5000)
