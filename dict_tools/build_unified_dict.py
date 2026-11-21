import urllib.request
import re

print("[1/4] 辞書群ダウンロード中 (Phase 3: 百科事典・全方位専門語・学術)...")
# 1. Mozc 基本辞書 (00〜09)
mozc_lines = []
for i in range(10):
    url = f"https://raw.githubusercontent.com/google/mozc/master/src/data/dictionary_oss/dictionary{i:02d}.txt"
    with urllib.request.urlopen(url) as res:
        mozc_lines.extend(res.read().decode('utf-8').splitlines())

# 2. SKK 専門辞書群（基本＋地名＋固有名＋駅名＋法律＋人名＋複合語＋Pubdic＋異体字＋追加）
skk_targets = [
    ("SKK-JISYO.L", 8000),          # 基本辞書
    ("SKK-JISYO.geo", 9000),        # 国内・世界地名
    ("SKK-JISYO.propernoun", 9500), # 企業・大学・固有名詞
    ("SKK-JISYO.station", 9500),    # 駅名・路線
    ("SKK-JISYO.law", 10000),       # 法律・行政用語
    ("SKK-JISYO.jinmei", 10000),    # 人名
    ("SKK-JISYO.fullname", 10500),  # 人名フルネーム
    ("SKK-JISYO.hukugougo", 11000), # 専門・慣用複合語
    ("SKK-JISYO.pubdic+", 11000),   # 大規模学術・一般語彙
    ("SKK-JISYO.itaiji", 11500),    # 異体字
    ("SKK-JISYO.assoc", 11500),     # 連想・関連語
]

skk_dict_data = []
for fname, default_cost in skk_targets:
    url = f"https://raw.githubusercontent.com/skk-dev/dict/master/{fname}"
    print(f"  DL中: {fname}...")
    try:
        with urllib.request.urlopen(url) as res:
            lines = res.read().decode('euc-jp', errors='ignore').splitlines()
            skk_dict_data.append((fname, default_cost, lines))
    except Exception as e:
        print(f"  スキップ ({fname}): {e}")

# ひらがな -> カタカナ変換関数
def hira_to_kata(hira):
    return "".join(chr(ord(c) + 96) if 'ぁ' <= c <= 'ん' else c for c in hira)

print("[2/4] コリジョン解決・コスト階層化・カタカナ救済生成中...")
entries = {}

# Mozc パース（日常頻出語: コスト 00000〜07000）
for line in mozc_lines:
    parts = line.split('\t')
    if len(parts) >= 5:
        yomi, cost, word = parts[0], int(parts[3]), parts[4]
        if len(yomi) > 20 or len(word) > 30: continue
        if re.search(r'[!-@\[-`{-~]', yomi): continue
        cost = max(0, min(99999, cost))
        key = (yomi, word)
        if key not in entries or cost < entries[key]:
            entries[key] = cost

# SKK 専門辞書群パース（専門・学術・固有名詞: コスト 08000〜11500）
for fname, default_cost, lines in skk_dict_data:
    for line in lines:
        if line.startswith(';') or not line: continue
        parts = line.split(' ', 1)
        if len(parts) < 2: continue
        yomi, cand_str = parts[0], parts[1]
        cands = [c.split(';')[0] for c in cand_str.strip('/').split('/') if c]
        for word in cands:
            if len(yomi) > 20 or len(word) > 30: continue
            if re.search(r'[!-@\[-`{-~]', yomi): continue
            key = (yomi, word)
            if key not in entries:
                entries[key] = default_cost

# 未知外来語・海外地名・人名救済：すべての読みに対してカタカナ表記をコスト 09800 で自動補完
# （日常漢字の邪魔をせず、外来語や新語・外国地名を打ったときに確実に候補に出す）
all_yomis = set(y for y, _ in entries.keys())
for yomi in all_yomis:
    kata = hira_to_kata(yomi)
    key = (yomi, kata)
    if key not in entries:
        entries[key] = 9800

print(f"[3/4] コスト順ソート＆TSV書き出し中 (総単語数: {len(entries)}語)...")
sorted_keys = sorted(entries.keys(), key=lambda x: (x[0], entries[x]))

with open("clean_dict.tsv", "w", encoding="utf-8") as f:
    for yomi, word in sorted_keys:
        cost = entries[(yomi, word)]
        f.write(f"{yomi}\t{cost:05d}\t{word}\n")

print("[4/4] marisa-build 実行中...")
