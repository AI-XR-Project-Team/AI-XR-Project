-- =====================================================================
-- 아르켈론(고대 바다거북) + 전시물 시드
--
-- 적용:  docker exec -i dino_ar_db psql -U dino -d dino_ar < seeds/05_dino_archelon.sql
--
-- 04_dinos_extra.sql 과 같은 규칙이다. 이름(name_ko)으로 중복을 막으므로 여러 번
-- 돌려도 안전하다. 앱 DA_Dino_Archelon 의 ExhibitKey 는 아래 model_asset_key
-- (archelon_full_skeleton)와 글자까지 같아야 도슨트 세션이 열린다.
--
-- ai_prompt_context 의 수치는 전시 문구 초안(약 4m, 약 2t, 백악기 후기)이며 기관
-- 자료와 대조 전이다. 단정적으로 확장하지 않는다.
-- =====================================================================
BEGIN;

INSERT INTO dinosaurs (name_ko, name_sci, period, length_m, model_asset_key, ai_prompt_context)
SELECT * FROM (VALUES
  ('아르켈론', 'Archelon ischyros', '백악기 후기', 4.00, 'archelon_full_skeleton',
   '백악기 후기(약 8,300만~7,000만 년 전) 북아메리카를 가로지르던 서부 내해(Western Interior Seaway)에 살았던, 지금까지 알려진 가장 큰 바다거북. 공룡이 아니라 해양 파충류다. 몸길이 약 4m, 추정 몸무게 약 2t. 매우 길고 강한 앞지느러미, 비교적 가벼운 등딱지, 단단한 부리가 특징이며 넓은 바다를 유영하도록 진화했다. 해파리·연체동물 등 부드러운 몸을 가진 해양 생물을 먹었다고 본다. 화석은 미국 사우스다코타의 피에르 셰일(해양 퇴적층)에서 발견됐고 19세기 후반에 학술적으로 기술됐다.')
) AS v(name_ko, name_sci, period, length_m, model_asset_key, ai_prompt_context)
WHERE NOT EXISTS (SELECT 1 FROM dinosaurs d WHERE d.name_ko = v.name_ko);

INSERT INTO exhibits (dinosaur_id, label, anchor_hint)
SELECT d.id, v.label, v.hint
FROM (VALUES
  ('아르켈론', '1관 아르켈론 전시존', '바닥 마커 EX5-ARCHELON 기준 정렬')
) AS v(name_ko, label, hint)
JOIN dinosaurs d ON d.name_ko = v.name_ko
WHERE NOT EXISTS (SELECT 1 FROM exhibits e WHERE e.dinosaur_id = d.id);

COMMIT;
