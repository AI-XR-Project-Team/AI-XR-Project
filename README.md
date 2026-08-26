# TimeMachineAR (가상의 공룡 박물관 AR)

![Unreal Engine](https://img.shields.io/badge/Unreal_Engine-5.4-black?logo=unrealengine)
![Android](https://img.shields.io/badge/Platform-Android_ARCore-3DDC84?logo=android)
![FastAPI](https://img.shields.io/badge/Backend-FastAPI-009688?logo=fastapi)
![PostgreSQL](https://img.shields.io/badge/Database-PostgreSQL-336791?logo=postgresql)


**TimeMachineAR**은 가상의 공룡 박물관을 배경으로, 관람객에게 압도적인 몰입감과 상호작용 경험을 제공하는 **지능형 AR 에듀테크 플랫폼**입니다. 
단순한 3D 모델 뷰어 앱을 넘어, 실내 정밀 측위 기반의 **AR 네비게이션**과 생성형 AI(LLM)가 결합된 실시간 **'AI 도슨트'**를 통해 현실의 전시 공간을 인터랙티브한 학습의 장으로 탈바꿈시킵니다.

---

##  주요 기능 및 특징 (Key Features)

### 1. 마커 트래킹 기반 고해상도 AR 렌더링
- **정밀한 실감형 콘텐츠**: Google ARCore를 활용하여 전시 공간에 부착된 마커를 인식하고, 티라노사우루스 등 고품질 3D 공룡(Skeletal Mesh)을 실제 공간에 정확한 스케일로 증강시킵니다.
- **직관적인 UX/UI 제어**: 복잡한 조작 없이 마커 인식만으로 오버레이(`DinoOverlayActor`)가 렌더링되며, 사용자 중심의 UI 제어(원터치 렌더링 종료 등)를 통해 자연스러운 AR 경험을 유지합니다.

### 2. 실내 AR 네비게이션 및 미니맵 (Wayfinding)
- **공간 동기화 로직 (`NavLocalizer`)**: 현실 물리 공간의 좌표계와 앱 내부의 3D 월드 좌표계를 수학적으로 매칭하여, 실내에서도 GPS 없이 끊김 없는 길안내를 제공합니다.
- **AR 가이드 및 미니맵 (`NavMinimapWidget`)**: 디스플레이상에 직관적인 AR 가이드라인을 투영하여 특정 공룡 전시물 앞까지 관람객을 안내합니다.

### 3. 생성형 AI 기반 맞춤형 도슨트 (AI Docent)
- **FastAPI 기반 백엔드 연동**: Unreal Engine 클라이언트와 Python 백엔드가 통신하여 관람객의 질문에 실시간으로 대응합니다.
- **LLM 스트리밍 답변 (`DocentChatWidget`)**: Gemini API 등을 활용해 공룡에 대한 깊이 있는 맞춤형 해설을 제공하며, Server-Sent Events(SSE) 기술로 답변을 지연 없이 스트리밍(타이핑 효과)으로 출력하여 진짜 해설사와 대화하는 듯한 경험을 줍니다.

---

## 🛠 기술 스택 (Tech Stack)

### Client (Frontend)
- **Engine**: Unreal Engine 5.4 (C++ & Blueprints)
- **AR Framework**: Google ARCore
- **UI/UX**: Unreal Motion Graphics (UMG)
- **Graphics API**: Vulkan (Mobile Optimized)

### Server (Backend)
- **Framework**: FastAPI (Python 3.10+)
- **Database**: PostgreSQL (psycopg2)
- **AI/LLM**: Google Gemini API (Mock 모드 테스트 환경 지원)
- **Architecture**: RESTful API & SSE (Server-Sent Events)

---

## 주요 아키텍처 및 코어 모듈
- `/TimeMachineAR/Source` : 언리얼 클라이언트 C++ 핵심 비즈니스 로직
  - `ARTrackingManager`: 실시간 AR 세션 상태 관리 및 마커 감지 델리게이트 시스템
  - `DocentChatWidget` & `DocentClient`: 비동기 HTTP 통신(SSE) 및 챗봇 UI 렌더링 제어
  - `NavLocalizer`: 3D 공간 벡터 기반 실내 측위 및 좌표 변환 매트릭스 계산
- `/backend_server/app` : AI 및 데이터 서빙 파이썬 서버
  - `routers/` & `services/`: 도슨트 채팅 컨텍스트 관리 및 LLM 프롬프트 파이프라인

---

## 실행 및 빌드 가이드 (Getting Started)

### 1. 백엔드 서버 구동
```bash
cd backend_server
python -m venv .venv
# 가상환경 활성화 (Mac/Linux: source .venv/bin/activate, Win: .venv\Scripts\activate)
pip install -r requirements.txt

# 환경변수 세팅 (.env 파일 생성)
# LLM_PROVIDER=gemini
# LLM_API_KEY=your_api_key_here

# 서버 실행 (기본 포트: 8000)
uvicorn app.main:app --reload
```
> API 명세서(Swagger UI)는 서버 구동 후 `http://localhost:8000/docs`에서 확인하실 수 있습니다.

### 2. 언리얼 클라이언트(App) 빌드
1. **Unreal Engine 5.4**에서 `TimeMachineAR.uproject` 오픈
2. Android 모바일 최적화를 위한 프로젝트 세팅:
   - `Use IO Store` 비활성화 (False)
   - `Package game data inside .apk?` 활성화 (True)
   - `Support OpenGL ES3.2` 비활성화 / `Support Vulkan` 활성화
3. `Platforms` > `Android` > `Package Project` 로 `.apk` 추출 후 실기기 설치

---

> **Note**: 본 리포지토리는 LFS(Large File Storage)를 사용합니다. Clone 시 반드시 `git lfs pull`을 수행하여 `.uasset`, `.fbx` 등의 대용량 그래픽 에셋을 동기화해야 정상적인 엔진 로딩 및 빌드가 가능합니다.
