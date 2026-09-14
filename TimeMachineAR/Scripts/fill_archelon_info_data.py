"""UE 5.4 Python 으로 실행. DA_Dino_Archelon 에 정보 화면(바다 스킨) 내용을 채운다.

import_archelon_info_skin.py 로 /Game/UI/DinoCard/ArchelonSkin 텍스처를 먼저 들여와야
한다(삽화·아이콘을 여기서 참조한다). C++ 의 CardSkin/ZoneName/탭별 Stats 필드가 있는
에디터가 빌드돼 있어야 한다.

DA_Dino_Archelon 은 처음에 티라노 DA 를 복사해 만든 것이라 12~13 m, 6~9 ton, T_Rex 사진
같은 값이 남아 있었다. 이 스크립트가 네 탭(소개·특징·서식·발견)과 태그·대표 이미지·
ExhibitKey 를 전부 덮어쓴다. 다른 종의 DA 는 건드리지 않는다.

문구·수치는 제공된 디자인 레퍼런스에서 옮긴 초안이다(학술 검증 전). 전시 최종 문구
확정 전에 기관 자료와 대조한다.

실행(헤드리스, 리포 루트에서):
  "<UE>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" TimeMachineAR/TimeMachineAR.uproject \
    -run=pythonscript -script="TimeMachineAR/Scripts/fill_archelon_info_data.py" -unattended -nosplash
"""
import unreal

DA_PATH = "/Game/UI/DinoCard/DA_Dino_Archelon"
SKIN = "/Game/UI/DinoCard/ArchelonSkin/"

# 서버 dinosaurs.model_asset_key (backend_server/seeds/05_dino_archelon.sql). 예전 값은
# 티라노 DA 를 복사하면서 딸려 온 trex_full_skeleton 이었다 — 도슨트가 티라노로 답하던 원인.
EXHIBIT_KEY = "archelon_full_skeleton"

ORANGE = unreal.LinearColor(1.0, 0.463, 0.196, 1.0)   # #FFB44D (sRGB → linear 근사)
CYAN = unreal.LinearColor(0.078, 0.700, 1.0, 1.0)     # #50DAFF


def tex(name):
    asset = unreal.load_asset(SKIN + name)
    if asset is None:
        raise RuntimeError("texture missing: " + SKIN + name + " (run import_archelon_info_skin.py first)")
    return asset


def text(s):
    return unreal.Text(s)


def stat(label, value, sub, icon, tint):
    st = unreal.DinoStat()
    st.set_editor_property("label", text(label))
    st.set_editor_property("value", text(value))
    st.set_editor_property("sub", text(sub))
    st.set_editor_property("icon", tex(icon))
    st.set_editor_property("icon_tint", tint)
    return st


def tab(label, icon, heading, body, image, stats, prompt):
    tb = unreal.DinoTab()
    tb.set_editor_property("label", text(label))
    tb.set_editor_property("icon", tex(icon))
    tb.set_editor_property("heading", text(heading))
    tb.set_editor_property("body", text(body))
    tb.set_editor_property("image", tex(image) if image else None)
    tb.set_editor_property("stats", stats)
    tb.set_editor_property("docent_prompt", text(prompt))
    return tb


INTRO_STATS = [
    stat("몸길이", "약 4m", "대형 바다거북", "T_IcoStatLength", ORANGE),
    stat("몸무게", "약 2톤", "추정 최대 개체", "T_IcoStatWeight", ORANGE),
    stat("식성", "육식·잡식", "해파리·연체동물 등", "T_IcoStatDiet", ORANGE),
    stat("시대", "백악기 후기", "약 8,300만 ~ 7,000만 년 전", "T_IcoStatEra", ORANGE),
]

TABS = [
    tab("소개", "T_IcoTabIntro",
        "고대 바다를 누빈 거대한 바다거북",
        "아르켈론(Archelon)은 백악기 후기(약 8,300만 ~ 7,000만 년 전)에 살았던, 지금까지 알려진 가장 큰 바다거북입니다. "
        "현대 바다거북보다 훨씬 큰 몸집으로 고대 바다를 자유롭게 헤엄치며, 다양한 해양 생물을 사냥한 최상위 포식자였습니다.",
        "T_IntroIllustration", INTRO_STATS,
        "아르켈론에 대해 더 자세히 알아보세요."),
    tab("특징", "T_IcoTabFeature",
        "넓은 바다를 누빈 유영 전문가",
        "아르켈론은 매우 길고 강력한 앞지느러미와 비교적 가벼운 등딱지, 단단한 부리를 지닌 거대 바다거북으로, "
        "넓은 바다를 자유롭게 유영하도록 진화한 최고의 유영 전문가였습니다.",
        "T_FeatureIllustration",
        [
            stat("앞지느러미", "길고 강력함", "유영 추진력", "T_IcoFlipper", CYAN),
            stat("등딱지", "비교적 가벼움", "넓은 유영에 적합", "T_IcoShell", CYAN),
            stat("부리", "강한 턱", "먹이를 쉽게 포획", "T_IcoBeak", CYAN),
            stat("이동", "넓은 유영 범위", "바다 전역 활동", "T_IcoWaves", CYAN),
        ],
        "아르켈론의 특징을 더 자세히 알아보세요."),
    tab("서식", "T_IcoTabHabitat",
        "북아메리카의 얕은 내해에서 살았어요",
        "아르켈론은 백악기 후기 북아메리카를 가로지르는 서부 내해(Western Interior Seaway)에서 살았습니다. "
        "이곳은 따뜻하고 비교적 얕은 내해로, 해파리와 연체동물 등 부드러운 몸을 가진 해양 생물들이 풍부해 아르켈론이 번성할 수 있었습니다.",
        "T_HabitatMap",
        [
            stat("지역", "북아메리카", "서부 내해", "T_IcoPin", ORANGE),
            stat("환경", "얕은 바다", "넓은 해양 환경", "T_IcoWaves2", ORANGE),
            stat("수온", "비교적 따뜻함", "온난한 바다", "T_IcoThermo", ORANGE),
            stat("먹이 환경", "해파리 풍부", "연체동물·무척추동물", "T_IcoJelly", ORANGE),
        ],
        "아르켈론이 어디에서 살았는지 더 자세히 알아보세요."),
    tab("발견", "T_IcoTabDiscovery",
        "화석이 들려준 고대 바다의 이야기",
        "아르켈론의 화석은 북아메리카에서 발견되었으며, 19세기 후반에 본격적으로 연구되었습니다. "
        "이 화석들은 과학자들이 백악기 후기의 거대한 바다거북을 이해하는 데 중요한 단서를 제공했습니다.",
        "T_DiscoverySkeleton",
        [
            stat("발견 지역", "사우스다코타", "북아메리카 화석지", "T_IcoPin", ORANGE),
            stat("지층", "피에르 셰일", "해양 퇴적층", "T_IcoStones", ORANGE),
            stat("연구", "19세기 후반", "학술적 기술", "T_IcoDoc", ORANGE),
            stat("의의", "거대 바다거북 연구", "해양 파충류 이해", "T_IcoCap", ORANGE),
        ],
        "아르켈론의 발견 이야기를 더 자세히 알아보세요."),
]

lib = unreal.EditorAssetLibrary
da = lib.load_asset(DA_PATH)
if da is None:
    raise RuntimeError("DA missing: " + DA_PATH)

da.set_editor_property("card_skin", unreal.DinoCardSkin.OCEAN)
da.set_editor_property("background", tex("T_OceanBackground"))
da.set_editor_property("zone_name", text("아르켈론 전시존"))
da.set_editor_property("zone_sub", text("고대 바다의 거대 거북"))
da.set_editor_property("docent_prompt", text("아르켈론에 대해 더 자세히 알아보세요."))

da.set_editor_property("display_number", text("05"))
da.set_editor_property("name_ko", text("아르켈론"))
da.set_editor_property("name_sci", text("Archelon"))
da.set_editor_property("diet_tag", text("해양 파충류"))
da.set_editor_property("diet_icon", tex("T_IcoMarine"))
da.set_editor_property("period_tag", text("백악기 후기"))
da.set_editor_property("period_sub", text("약 8,300만 ~ 7,000만 년 전"))
da.set_editor_property("period_icon", tex("T_IcoEra"))
da.set_editor_property("hero_image", tex("T_ArchelonHero"))
da.set_editor_property("intro_text", text(""))
da.set_editor_property("tabs", TABS)
da.set_editor_property("stats", INTRO_STATS)      # 탭 타일이 비었을 때의 공통 폴백

old_key = da.get_editor_property("exhibit_key")
da.set_editor_property("exhibit_key", EXHIBIT_KEY)

# 마커·메시·트랜스폼은 건드리지 않는다(AR 배치는 이번 작업 범위 밖).
marker = da.get_editor_property("marker_code")

if not lib.save_asset(DA_PATH):
    raise RuntimeError("save failed: " + DA_PATH)
unreal.log("ARCHELON_DATA_OK: tabs=%d exhibit_key %r -> %r marker=%r" % (len(TABS), old_key, EXHIBIT_KEY, marker))
