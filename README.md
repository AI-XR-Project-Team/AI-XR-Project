# 🦕 TimeMachineAR (AI-XR-Project)

![Unreal Engine](https://img.shields.io/badge/Unreal_Engine-5.4-black?logo=unrealengine)
![Android](https://img.shields.io/badge/Platform-Android_ARCore-3DDC84?logo=android)
![Status](https://img.shields.io/badge/Status-In_Development-blue)

**TimeMachineAR**은 박물관 및 전시 공간에서 관람객에게 몰입감 넘치는 경험을 제공하기 위해 기획된 **증강현실(AR) 및 인공지능(AI) 결합 통합 솔루션**입니다. 언리얼 엔진 5(Unreal Engine 5)를 기반으로 개발되었으며, 현실 공간을 인식하여 길을 안내하고, 살아 숨 쉬는 3D 공룡을 소환하며, AI가 전시물을 설명해 주는 스마트 관람 환경을 제공합니다.

---

## ✨ 핵심 기능 (Key Features)

### 1. 🦖 AR 공룡 (AR Dinosaur Rendering)
* **마커 기반 3D 소환**: 전시장에 배치된 특정 이미지 마커(포스터 등)를 스마트폰 카메라로 비추면, 해당 마커의 3D 공간 위에 실물 크기의 고퀄리티 3D 공룡(티라노사우루스, 트리케라톱스 등)이 소환됩니다.
* **실감 나는 애니메이션**: 소환된 공룡들은 정지된 조각상이 아닌, 숨을 쉬고 움직이는 스켈레탈 메시(Skeletal Mesh)와 애니메이션이 적용되어 관람객에게 생생한 현장감을 전달합니다.

### 2. 🗺️ AR 네비게이션 (AR Navigation & Wayfinding)
* **실내 길찾기 시스템**: 전시장이 넓고 복잡해도 문제없습니다. 마커를 스캔하여 관람객의 현재 위치(Transform)를 동기화하고, 가야 할 방향을 AR 화면 상에 직관적으로 안내합니다.
* **마커 통합**: 길찾기용 마커와 AR 렌더링용 마커를 하나로 통합하여, 관람객이 여러 번 스캔할 필요 없이 자연스럽게 위치를 파악하고 전시물을 관람할 수 있는 최적의 UX를 제공합니다.

### 3. 🤖 AI 도슨트 (AI Docent)
* **스마트 인공지능 가이드**: AR로 소환된 공룡이나 전시물에 대한 심도 있는 정보를 AI 도슨트가 제공합니다.
* **인터랙티브 정보 제공**: 텍스트나 음성을 통해 마치 실제 큐레이터와 동행하는 것처럼 쾌적하고 유익한 전시 관람을 지원합니다.

---

## 🛠 기술 스택 (Tech Stack)

* **Game Engine**: Unreal Engine 5.4
* **AR Platform**: Google ARCore
* **Target Platform**: Android (Min SDK 24, Target SDK 32+)
* **Graphics API**: Vulkan (Mobile 최적화)
* **3D Modeling & Animation**: Blender, AI Generated Models (Meshy 등)
* **Version Control**: Git & Git LFS (GitHub)

---

## ⚙️ 빌드 및 실행 방법 (Build Instructions)

본 프로젝트는 안드로이드(Android) 모바일 환경에 최적화되어 있습니다.

### 사전 요구 사항
* Unreal Engine 5.4.x 설치
* Android Studio 및 NDK (UE5.4 권장 버전 세팅 완료 필요)
* ARCore를 지원하는 안드로이드 스마트폰

### 프로젝트 설정 가이드
안드로이드 기기에서 패키징 시 앱이 튕기거나(Crash) 로딩 에러가 발생하는 것을 방지하기 위해 다음 세팅을 반드시 유지해야 합니다.
1. `Edit` > `Project Settings` > `Packaging`에서 **Use IO Store** 체크 해제 (`False`)
2. `Edit` > `Project Settings` > `Platforms` > `Android`에서 **Package game data inside .apk?** 체크 (`True`)
3. `Edit` > `Project Settings` > `Platforms` > `Android` > `Build` 섹션에서:
   * **Support OpenGL ES3.2** 체크 해제 (`False`)
   * **Support Vulkan** 체크 (`True`)

### 패키징
1. `Platforms` > `Android` > `Package Project` 클릭
2. 생성된 폴더 내의 `Install_TimeMachineAR-arm64.bat` 파일을 실행하여 스마트폰에 앱 설치

---

## 👥 팀 (Team AI-XR-Project)
* **프로젝트명**: AI-XR-Project
* **저장소**: [AI-XR-Project-Team/AI-XR-Project](https://github.com/AI-XR-Project-Team/AI-XR-Project)

> 본 리포지토리는 LFS(Large File Storage)를 사용하여 `.uasset`, `.umap`, `.fbx`, `.png` 등의 에셋을 관리합니다. 클론(Clone) 시 `git lfs pull` 명령어가 필요할 수 있습니다.
