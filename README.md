# MindFlora
Circuit of &lt;MindFlora> SmartPottery project
Image Below (V1)

![](MindFlora_bb.jpg)

# MindAI
Add Emotion & Memories(Long-T, Short-T) with Prompting and Data management
But, this is not a real making Memories it's a kind of efficient way to manage Database of chating

Main Idea
- all chat has this and AI could get 3 kewords and load a Emotion keword of it. this is the source of Impression Weights.
- Impression Weights : A main value of impression

this Weights gets out from these weights
- valence: Value of positive or nagative (0 ~ 2) -> 10%
- arousal: Value of depth of emotion (-1~1) -> 40%
- impressiveness: Value of expression power (0~1) -> 50%

this is on writing --

# Eval
사용자: 우와 나 상받았어 (인상도: 8.13, 키워드: 수상, 기쁨, 자랑, 감정: 기쁨)
사용자: 오늘 어땠어? (인상도: 2.50, 키워드: 오늘, 어땠어, 질문, 감정: 중립)
사용자: 아니 xx xxx야 (인상도: 6.38, 키워드: 욕설, 분노, 비속어, 감정: 분노)
사용자: 오늘 숙제를 못한것 같아.. (인상도: 4.25, 키워드: 숙제, 미완성, 걱정, 감정: 불안)

this is a example of it

this is on testing --
