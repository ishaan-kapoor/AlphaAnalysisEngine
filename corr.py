import pandas as pd

out = "<output_dir>"
sig = "<signal_name>" # e.g. "top_book_imbalance"

for sym in ("<symbols>",):
    df = pd.read_csv(f"{out}/{sym}.csv")
    tgts = ["<targets>",] # e.g. ["fair_price_ret_1s"]
    c = df[[sig] + tgts].corr().loc[sig, tgts]
    print(f"{sym}  n={len(df)}")
    for t in tgts: print(f"  {sig:18s} vs {t:20s} {c[t]:+.4f}")