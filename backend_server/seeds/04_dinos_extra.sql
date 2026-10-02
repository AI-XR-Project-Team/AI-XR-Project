-- =====================================================================
-- 공룡 3종(트리케라톱스·브라키오사우루스·안킬로사우루스) + 전시물 시드
--
-- 적용:  docker exec -i dino_ar_db psql -U dino -d dino_ar < seeds/04_dinos_extra.sql
--
-- 이름(name_ko)으로 중복을 막으므로 여러 번 돌려도 안전하다. exhibits.id 는
-- 자동 생성 UUID 라 머신마다 다르다 — 적용 후 아래로 조회해 각자
-- DA_Dino_* 의 ExhibitId 에 채운다.
--   SELECT d.name_ko, e.id FROM exhibits e JOIN dinosaurs d ON d.id = e.dinosaur_id;
--
-- ai_prompt_context 는 AI 도슨트가 답변을 만들 때 쓰는 배경 지식이다.
-- 비워 두면 종을 구분하지 못하고 일반적인 공룡 이야기만 한다.
-- =====================================================================
BEGIN;

INSERT INTO dinosaurs (name_ko, name_sci, period, length_m, model_asset_key, ai_prompt_context)
SELECT * FROM (VALUES
  ('트리케라톱스', 'Triceratops horridus', '백악기 후기', 8.50, 'triceratops_full_skeleton',
   '백악기 후기(약 6,800만 년 전) 북아메리카에 살았던 대형 초식 공룡. 코 위 짧은 뿔 하나와 눈 위 긴 뿔 두 개, 목을 감싼 커다란 골질 프릴이 특징이다. 앵무새 같은 부리로 질긴 식물을 자르고 촘촘한 이빨로 갈아 먹었다. 티라노사우루스와 같은 시대·같은 지역에 살았다. 몸길이 8~9m, 몸무게 6~12t. 1889년 오스니얼 찰스 마시가 명명했다.'),
  ('브라키오사우루스', 'Brachiosaurus altithorax', '쥐라기 후기', 23.00, 'brachiosaurus_full_skeleton',
   '쥐라기 후기(약 1억 5천만 년 전) 북아메리카에 살았던 거대 초식 공룡(용각류). 앞다리가 뒷다리보다 길어 어깨가 높이 솟은 독특한 체형이며, 긴 목으로 다른 공룡이 닿지 못하는 높이의 나뭇잎을 먹었다. 머리 위 아치형 콧등 볏과 숟가락 모양 이빨이 특징이다. 몸길이 21~25m, 몸무게 28~58t. 1900년 콜로라도에서 엘머 릭스가 발굴해 1903년 명명했고, 이름은 ''팔 도마뱀''이라는 뜻이다.'),
  ('안킬로사우루스', 'Ankylosaurus magniventris', '백악기 후기', 7.00, 'ankylosaurus_full_skeleton',
   '백악기 후기(약 6,800만 년 전) 북아메리카에 살았던 장갑 초식 공룡. 등과 옆구리가 뼈판(골편)과 가시로 덮여 있고 눈꺼풀에도 뼈가 있다. 꼬리 끝의 커다란 곤봉으로 포식자의 다리뼈를 부술 만한 타격을 냈다. 몸을 낮춘 자세로 지면 가까운 부드러운 식물을 먹었다. 몸길이 6~8m, 몸무게 4~8t. 1906년 몬태나주에서 발굴돼 1908년 바넘 브라운이 명명했으며, 이름은 ''굽은 도마뱀''이라는 뜻이다.')
) AS v(name_ko, name_sci, period, length_m, model_asset_key, ai_prompt_context)
WHERE NOT EXISTS (SELECT 1 FROM dinosaurs d WHERE d.name_ko = v.name_ko);

INSERT INTO exhibits (dinosaur_id, label, anchor_hint)
SELECT d.id, v.label, v.hint
FROM (VALUES
  ('트리케라톱스',     '1관 트리케라톱스 전신골격',     '중앙 홀 바닥 마커 A2 기준 정렬'),
  ('브라키오사우루스', '1관 브라키오사우루스 전신골격', '중앙 홀 바닥 마커 A3 기준 정렬'),
  ('안킬로사우루스',   '1관 안킬로사우루스 전신골격',   '중앙 홀 바닥 마커 A4 기준 정렬')
) AS v(name_ko, label, hint)
JOIN dinosaurs d ON d.name_ko = v.name_ko
WHERE NOT EXISTS (SELECT 1 FROM exhibits e WHERE e.dinosaur_id = d.id);

COMMIT;
