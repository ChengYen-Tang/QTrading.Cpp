import argparse
import os
import sys
from dataclasses import dataclass
from typing import Dict, Optional, Tuple

import pandas as pd
import plotly.express as px
import pyarrow as pa
import streamlit as st

ACCOUNT_LEDGER_MAP = {-1: "Unknown", 0: "Perp", 1: "Spot", 2: "Both"}


@dataclass
class ArrowData:
    name: str
    path: str
    rows: int
    df: Optional[pd.DataFrame]


def _parse_cli_logs_dir() -> Optional[str]:
    if "--logs" in sys.argv:
        idx = sys.argv.index("--logs")
        if idx + 1 < len(sys.argv):
            return sys.argv[idx + 1]
    return None


@st.cache_data(show_spinner=False)
def _read_arrow_df(path: str) -> Optional[pd.DataFrame]:
    if not os.path.exists(path):
        return None
    if os.path.getsize(path) == 0:
        return None
    with pa.memory_map(path, "r") as source:
        reader = pa.ipc.RecordBatchFileReader(source)
        table = reader.read_all()
    if table is None:
        return None
    return table.to_pandas()


def _load_arrow_files(logs_dir: str) -> Dict[str, ArrowData]:
    out: Dict[str, ArrowData] = {}
    if not os.path.isdir(logs_dir):
        return out
    for name in os.listdir(logs_dir):
        if not name.lower().endswith(".arrow"):
            continue
        path = os.path.join(logs_dir, name)
        df = _read_arrow_df(path)
        rows = 0 if df is None else len(df)
        out[name] = ArrowData(name=name, path=path, rows=rows, df=df)
    return out


def _list_run_dirs(logs_root: str) -> list[str]:
    if not os.path.isdir(logs_root):
        return []
    run_dirs = []
    for name in os.listdir(logs_root):
        path = os.path.join(logs_root, name)
        if os.path.isdir(path):
            run_dirs.append(name)
    run_dirs.sort(reverse=True)
    return run_dirs


def _load_run_metadata_json(logs_dir: str) -> Optional[dict]:
    path = os.path.join(logs_dir, "run_metadata.json")
    if not os.path.exists(path):
        return None
    try:
        return pd.read_json(path, typ="series").to_dict()
    except Exception:
        return None


def _load_dataset_paths_json(logs_dir: str) -> Optional[dict]:
    path = os.path.join(logs_dir, "dataset_paths.json")
    if not os.path.exists(path):
        return None
    try:
        return pd.read_json(path, typ="series").to_dict()
    except Exception:
        return None


def _ts_to_dt(series: pd.Series) -> pd.Series:
    return pd.to_datetime(series, unit="ms", errors="coerce")


def _add_time_index(df: pd.DataFrame, ts_col: str) -> pd.DataFrame:
    if ts_col in df.columns:
        df = df.copy()
        df["datetime"] = _ts_to_dt(df[ts_col])
        df["time"] = df["datetime"].dt.strftime("%Y-%m-%d %H:%M:%S")
    return df


def _cache_key_for(path: str) -> str:
    try:
        return f"{path}:{os.path.getmtime(path)}:{os.path.getsize(path)}"
    except OSError:
        return path


def _first_existing_col(df: pd.DataFrame, candidates: list[str]) -> Optional[str]:
    for name in candidates:
        if name in df.columns:
            return name
    return None


def _compute_equity_df(account_evt: pd.DataFrame) -> Optional[pd.DataFrame]:
    if account_evt is None or account_evt.empty:
        return None

    df = account_evt.copy()
    if "ts" not in df.columns:
        return None

    df = _add_time_index(df, "ts")
    df = df[df["datetime"].notna()].sort_values("datetime")

    equity_col = _first_existing_col(
        df,
        [
            "total_ledger_value_after",
            "perp_margin_balance_after",
            "margin_balance_after",
            "wallet_balance_after",
        ],
    )
    if equity_col is None:
        return None

    df["equity"] = df[equity_col]
    df["equity_source"] = equity_col
    df["is_fill_snapshot"] = (df.get("request_id", 0) > 0)
    return df


def _build_ledger_mix_df(account_evt: pd.DataFrame) -> Optional[pd.DataFrame]:
    if account_evt is None or account_evt.empty:
        return None

    df = account_evt.copy()
    if "ts" not in df.columns:
        return None
    df = _add_time_index(df, "ts")
    df = df[df["datetime"].notna()].sort_values("datetime")

    spot_col = _first_existing_col(df, ["spot_ledger_value_after", "spot_wallet_balance_after"])
    perp_col = _first_existing_col(df, ["perp_margin_balance_after", "margin_balance_after", "wallet_balance_after"])
    total_col = _first_existing_col(df, ["total_ledger_value_after"])
    if spot_col is None or perp_col is None:
        return None

    out = pd.DataFrame({
        "datetime": df["datetime"],
        "spot_ledger": pd.to_numeric(df[spot_col], errors="coerce"),
        "perp_ledger": pd.to_numeric(df[perp_col], errors="coerce"),
    })
    if total_col is not None:
        out["total_ledger"] = pd.to_numeric(df[total_col], errors="coerce")
    else:
        out["total_ledger"] = out["spot_ledger"] + out["perp_ledger"]

    out = out.dropna(subset=["datetime", "spot_ledger", "perp_ledger", "total_ledger"])
    out = out[out["total_ledger"] > 0.0]
    if out.empty:
        return None

    out["spot_ratio"] = out["spot_ledger"] / out["total_ledger"]
    out["perp_ratio"] = out["perp_ledger"] / out["total_ledger"]
    return out


def _compute_drawdown(equity: pd.Series) -> pd.Series:
    running_max = equity.cummax()
    return (equity / running_max) - 1.0


def _estimate_period_seconds(times: pd.Series) -> Optional[float]:
    if times is None or times.size < 3:
        return None
    diffs = times.diff().dropna().dt.total_seconds()
    diffs = diffs[diffs > 0]
    if diffs.empty:
        return None
    return float(diffs.median())


def _compute_perf_metrics(equity_df: pd.DataFrame) -> Dict[str, Optional[float]]:
    metrics: Dict[str, Optional[float]] = {
        "total_return": None,
        "cagr": None,
        "volatility": None,
        "sharpe": None,
        "sortino": None,
        "max_drawdown": None,
    }
    if equity_df is None or equity_df.empty:
        return metrics

    equity = equity_df["equity"].astype(float)
    times = equity_df["datetime"]
    if equity.size < 3:
        return metrics

    total_return = (equity.iloc[-1] / equity.iloc[0]) - 1.0
    metrics["total_return"] = total_return

    period_seconds = _estimate_period_seconds(times)
    if period_seconds:
        seconds_per_year = 365.25 * 24 * 3600
        years = (times.iloc[-1] - times.iloc[0]).total_seconds() / seconds_per_year
        if years > 0:
            metrics["cagr"] = (equity.iloc[-1] / equity.iloc[0]) ** (1.0 / years) - 1.0

        returns = equity.pct_change().dropna()
        if returns.size > 2:
            ann_factor = seconds_per_year / period_seconds
            metrics["volatility"] = returns.std() * (ann_factor ** 0.5)
            if returns.std() > 0:
                metrics["sharpe"] = returns.mean() * (ann_factor ** 0.5) / returns.std()
            downside = returns[returns < 0]
            if downside.size > 1 and downside.std() > 0:
                metrics["sortino"] = returns.mean() * (ann_factor ** 0.5) / downside.std()

    metrics["max_drawdown"] = _compute_drawdown(equity).min()
    return metrics


def _compute_drawdown_recovery_days(equity_df: pd.DataFrame) -> Optional[int]:
    if equity_df is None or equity_df.empty:
        return None
    equity = equity_df["equity"].astype(float)
    times = equity_df["datetime"]
    if equity.size < 3:
        return None
    running_max = equity.cummax()
    drawdown = equity / running_max - 1.0
    trough_idx = drawdown.idxmin()
    peak_idx = equity[:trough_idx + 1].idxmax()
    peak_time = times.loc[peak_idx]
    trough_time = times.loc[trough_idx]
    if trough_time <= peak_time:
        return None
    recovery = equity.loc[trough_idx:]
    recovered = recovery[recovery >= equity.loc[peak_idx]]
    if recovered.empty:
        return None
    recovery_time = times.loc[recovered.index[0]]
    return int((recovery_time - peak_time).days)


def _build_daily_equity(equity_df: pd.DataFrame) -> Optional[pd.DataFrame]:
    if equity_df is None or equity_df.empty:
        return None
    df = equity_df.copy()
    df = df.set_index("datetime").sort_index()
    daily = df["equity"].resample("1D").last().dropna()
    if daily.empty:
        return None
    out = daily.to_frame("equity")
    out["returns"] = out["equity"].pct_change().fillna(0.0)
    return out


def _build_daily_benchmark(market_evt: pd.DataFrame, symbol: str) -> Optional[pd.DataFrame]:
    if market_evt is None or market_evt.empty:
        return None
    if "symbol" not in market_evt.columns or "close" not in market_evt.columns:
        return None
    df = market_evt[market_evt["symbol"] == symbol].copy()
    if df.empty:
        return None
    df = _add_time_index(df, "ts")
    df = df[df["datetime"].notna()].sort_values("datetime")
    daily = df.set_index("datetime")["close"].resample("1D").last().dropna()
    if daily.empty:
        return None
    out = daily.to_frame("bench_close")
    out["bench_returns"] = out["bench_close"].pct_change().fillna(0.0)
    return out


def _compute_alpha_beta(strategy: pd.Series, benchmark: pd.Series) -> Tuple[Optional[float], Optional[float], Optional[float]]:
    if strategy.size < 3 or benchmark.size < 3:
        return None, None, None
    aligned = pd.concat([strategy, benchmark], axis=1, join="inner").dropna()
    if aligned.shape[0] < 3:
        return None, None, None
    x = aligned.iloc[:, 1]
    y = aligned.iloc[:, 0]
    var = x.var()
    if var == 0:
        return None, None, None
    beta = (x.cov(y)) / var
    alpha = y.mean() - beta * x.mean()
    # Annualize alpha assuming daily returns.
    alpha_ann = alpha * 365.0
    return alpha_ann, beta, var


@st.cache_data(show_spinner=False, hash_funcs={pd.DataFrame: id})
def _cached_perf_metrics(equity_df: pd.DataFrame, cache_key: str) -> Dict[str, Optional[float]]:
    _ = cache_key
    return _compute_perf_metrics(equity_df)


@st.cache_data(show_spinner=False, hash_funcs={pd.DataFrame: id})
def _cached_drawdown_recovery(equity_df: pd.DataFrame, cache_key: str) -> Optional[int]:
    _ = cache_key
    return _compute_drawdown_recovery_days(equity_df)


@st.cache_data(show_spinner=False, hash_funcs={pd.DataFrame: id})
def _cached_daily_equity(equity_df: pd.DataFrame, cache_key: str) -> Optional[pd.DataFrame]:
    _ = cache_key
    return _build_daily_equity(equity_df)


@st.cache_data(show_spinner=False, hash_funcs={pd.DataFrame: id})
def _cached_rolling_table(daily_eq: pd.DataFrame, cache_key: str, metric: str) -> Optional[pd.DataFrame]:
    _ = cache_key
    return _rolling_stats_table(daily_eq, metric)


@st.cache_data(show_spinner=False, hash_funcs={pd.DataFrame: id})
def _cached_benchmark_metrics(
    equity_df: pd.DataFrame,
    market_df: pd.DataFrame,
    cache_key_equity: str,
    cache_key_market: str,
    bench_symbol: str,
) -> Dict[str, Optional[float]]:
    _ = (cache_key_equity, cache_key_market)
    daily_eq = _build_daily_equity(equity_df)
    daily_bm = _build_daily_benchmark(market_df, bench_symbol)
    if daily_eq is None or daily_bm is None:
        return {"alpha": None, "beta": None, "info_ratio": None, "tracking_error": None, "treynor": None}
    merged = daily_eq.join(daily_bm, how="inner")
    if merged.empty:
        return {"alpha": None, "beta": None, "info_ratio": None, "tracking_error": None, "treynor": None}
    alpha, beta, _ = _compute_alpha_beta(merged["returns"], merged["bench_returns"])
    active = merged["returns"] - merged["bench_returns"]
    info_ratio = None
    tracking_error = None
    if active.std() > 0:
        info_ratio = active.mean() * (365 ** 0.5) / active.std()
        tracking_error = active.std() * (365 ** 0.5)
    treynor = None
    if beta is not None and beta != 0:
        treynor = merged["returns"].mean() * 365.0 / beta
    return {
        "alpha": alpha,
        "beta": beta,
        "info_ratio": info_ratio,
        "tracking_error": tracking_error,
        "treynor": treynor,
    }


@st.cache_data(show_spinner=False, hash_funcs={pd.DataFrame: id})
def _cached_capacity_turnover(
    fills_df: pd.DataFrame,
    equity_df: pd.DataFrame,
    market_df: pd.DataFrame,
    cache_key_fills: str,
    cache_key_equity: str,
    cache_key_market: str,
    participation_rate: float,
) -> Dict[str, Optional[object]]:
    _ = (cache_key_fills, cache_key_equity, cache_key_market)
    result: Dict[str, Optional[object]] = {
        "by_symbol": None,
        "total_traded": None,
        "avg_equity": None,
        "turnover": None,
        "adv": None,
        "capacity": None,
        "lowest_capacity_asset": None,
    }
    if fills_df is None or fills_df.empty:
        return result
    fills = fills_df.copy()
    if "exec_qty" not in fills.columns or "exec_price" not in fills.columns:
        return result
    fills["notional"] = fills["exec_qty"].fillna(0.0) * fills["exec_price"].fillna(0.0)
    by_symbol = fills.groupby("symbol", dropna=False)["notional"].sum().reset_index()
    result["by_symbol"] = by_symbol
    total_traded = fills["notional"].sum()
    avg_equity = equity_df["equity"].mean() if equity_df is not None and not equity_df.empty else None
    result["total_traded"] = total_traded
    result["avg_equity"] = avg_equity
    result["turnover"] = (total_traded / avg_equity) if avg_equity and avg_equity > 0 else None

    market_df = _add_time_index(market_df, "ts")
    if "symbol" not in market_df.columns or "close" not in market_df.columns or "volume" not in market_df.columns:
        return result
    traded_symbols = sorted(fills["symbol"].dropna().unique())
    if not traded_symbols:
        return result
    market_df = market_df[market_df["symbol"].isin(traded_symbols)].copy()
    market_df["dollar_volume"] = market_df["close"].fillna(0.0) * market_df["volume"].fillna(0.0)
    daily = market_df.set_index("datetime").groupby("symbol")["dollar_volume"].resample("1D").sum().reset_index()
    adv = daily.groupby("symbol")["dollar_volume"].mean().reset_index()
    adv["capacity"] = adv["dollar_volume"] * participation_rate
    result["adv"] = adv
    result["capacity"] = adv["capacity"].sum() if not adv.empty else None
    result["lowest_capacity_asset"] = adv.sort_values("capacity").iloc[0]["symbol"] if not adv.empty else None
    return result


def _rolling_stats_table(daily: pd.DataFrame, metric: str) -> Optional[pd.DataFrame]:
    if daily is None or daily.empty:
        return None

    windows = {"1M": 30, "3M": 90, "6M": 180, "12M": 365}
    month_ends = daily.resample("M").last().index

    def window_metric(slice_df: pd.DataFrame) -> Optional[float]:
        if slice_df.empty:
            return None
        if metric == "Average Return":
            return slice_df["returns"].mean()
        if metric == "Sharpe":
            if slice_df["returns"].std() == 0:
                return None
            return slice_df["returns"].mean() * (365 ** 0.5) / slice_df["returns"].std()
        if metric == "Volatility":
            return slice_df["returns"].std() * (365 ** 0.5)
        if metric == "Max Drawdown":
            return _compute_drawdown(slice_df["equity"]).min()
        return None

    rows = []
    for end in month_ends:
        row = {"month": end.date().isoformat()}
        for label, days in windows.items():
            start = end - pd.Timedelta(days=days)
            slice_df = daily.loc[(daily.index > start) & (daily.index <= end)]
            row[label] = window_metric(slice_df)
        rows.append(row)
    return pd.DataFrame(rows)


def _format_pct(value: Optional[float]) -> str:
    return f"{value:.2%}" if value is not None else "n/a"


def _format_num(value: Optional[float]) -> str:
    return f"{value:,.2f}" if value is not None else "n/a"


def _format_ratio(value: Optional[float]) -> str:
    return f"{value:.2f}" if value is not None else "n/a"


def render_summary(data: Dict[str, ArrowData]):
    account_evt = data.get("AccountEvent.arrow")
    order_evt = data.get("OrderEvent.arrow")
    funding_evt = data.get("FundingEvent.arrow")

    start_equity = None
    end_equity = None
    end_spot_share = None
    end_perp_share = None
    wallet_delta_sum = None
    fee_sum = None
    funding_sum = None
    residual_price_pnl = None

    if account_evt and account_evt.df is not None and not account_evt.df.empty:
        df = account_evt.df
        equity_df = _compute_equity_df(df)
        if equity_df is not None and not equity_df.empty:
            start_equity = equity_df["equity"].iloc[0]
            end_equity = equity_df["equity"].iloc[-1]

        spot_col = _first_existing_col(df, ["spot_ledger_value_after", "spot_wallet_balance_after"])
        perp_col = _first_existing_col(df, ["perp_margin_balance_after", "margin_balance_after", "wallet_balance_after"])
        total_col = _first_existing_col(df, ["total_ledger_value_after"])
        if spot_col and perp_col:
            spot_end = float(df[spot_col].iloc[-1])
            perp_end = float(df[perp_col].iloc[-1])
            total_end = float(df[total_col].iloc[-1]) if total_col else (spot_end + perp_end)
            if total_end > 0:
                end_spot_share = spot_end / total_end
                end_perp_share = perp_end / total_end

        if "wallet_delta" in df.columns:
            wallet_delta_sum = df["wallet_delta"].sum()

    if order_evt and order_evt.df is not None and not order_evt.df.empty:
        df = order_evt.df
        if "fee" in df.columns:
            fee_sum = df["fee"].sum()

    if funding_evt and funding_evt.df is not None and not funding_evt.df.empty:
        df = funding_evt.df
        if "funding" in df.columns:
            funding_sum = df["funding"].sum()

    if wallet_delta_sum is not None and funding_sum is not None and fee_sum is not None:
        residual_price_pnl = wallet_delta_sum - funding_sum - fee_sum

    cols = st.columns(8)
    cols[0].metric("Start Equity", f"{start_equity:,.2f}" if start_equity is not None else "n/a")
    cols[1].metric("End Equity", f"{end_equity:,.2f}" if end_equity is not None else "n/a")
    cols[2].metric("End Spot Share", f"{end_spot_share:.2%}" if end_spot_share is not None else "n/a")
    cols[3].metric("End Perp Share", f"{end_perp_share:.2%}" if end_perp_share is not None else "n/a")
    cols[4].metric("Wallet Delta Sum", f"{wallet_delta_sum:,.2f}" if wallet_delta_sum is not None else "n/a")
    cols[5].metric("Funding Sum", f"{funding_sum:,.2f}" if funding_sum is not None else "n/a")
    cols[6].metric("Total Fees", f"{fee_sum:,.2f}" if fee_sum is not None else "n/a")
    cols[7].metric("Residual Price/Basis", f"{residual_price_pnl:,.2f}" if residual_price_pnl is not None else "n/a")


def render_account_event(data: ArrowData):
    df = data.df
    if df is None or df.empty:
        st.info("No AccountEvent data.")
        return

    df = _add_time_index(df, "ts")

    if "ledger" in df.columns:
        df["ledger_label"] = df["ledger"].map(ACCOUNT_LEDGER_MAP).fillna(df["ledger"].astype(str))

    chart_cols = []
    for col in [
        "total_ledger_value_after",
        "spot_ledger_value_after",
        "perp_margin_balance_after",
        "spot_wallet_balance_after",
        "perp_wallet_balance_after",
        "wallet_balance_after",
        "margin_balance_after",
        "available_balance_after",
    ]:
        if col in df.columns and col not in chart_cols:
            chart_cols.append(col)
    if not chart_cols:
        st.info("AccountEvent has no balance columns.")
        return

    fig = px.line(df, x="datetime", y=chart_cols, title="Account Balances (Dual Ledger)")
    st.plotly_chart(fig, use_container_width=True)

    spot_col = _first_existing_col(df, ["spot_ledger_value_after", "spot_wallet_balance_after"])
    perp_col = _first_existing_col(df, ["perp_margin_balance_after", "margin_balance_after", "wallet_balance_after"])
    total_col = _first_existing_col(df, ["total_ledger_value_after"])
    if spot_col and perp_col:
        mix = df[["datetime", spot_col, perp_col]].copy()
        if total_col:
            mix["total_ledger"] = df[total_col]
        else:
            mix["total_ledger"] = mix[spot_col] + mix[perp_col]
        mix = mix[mix["total_ledger"] > 0]
        if not mix.empty:
            mix["spot_ratio"] = mix[spot_col] / mix["total_ledger"]
            mix["perp_ratio"] = mix[perp_col] / mix["total_ledger"]
            fig_ratio = px.line(
                mix,
                x="datetime",
                y=["spot_ratio", "perp_ratio"],
                title="Ledger Value Ratio (Spot vs Perp)",
            )
            st.plotly_chart(fig_ratio, use_container_width=True)

    st.dataframe(df.tail(50), use_container_width=True)


def render_order_event(data: ArrowData):
    df = data.df
    if df is None or df.empty:
        st.info("No OrderEvent data.")
        return

    df = _add_time_index(df, "ts")

    if "event_type" in df.columns:
        type_map = {0: "Accepted", 1: "Rejected", 2: "Canceled", 3: "Filled"}
        df["event_type_label"] = df["event_type"].map(type_map).fillna(df["event_type"].astype(str))
        fig = px.histogram(df, x="event_type_label", title="Order Events")
        st.plotly_chart(fig, use_container_width=True)

    if "fee" in df.columns and "datetime" in df.columns:
        fee_df = df[df["fee"].notna()]
        if not fee_df.empty:
            fig = px.scatter(fee_df, x="datetime", y="fee", title="Order Fees")
            st.plotly_chart(fig, use_container_width=True)

    st.dataframe(df.tail(50), use_container_width=True)


def render_position_event(data: ArrowData):
    df = data.df
    if df is None or df.empty:
        st.info("No PositionEvent data.")
        return

    df = _add_time_index(df, "ts")

    if "event_type" in df.columns:
        type_map = {0: "Snapshot", 1: "Opened", 2: "Increased", 3: "Reduced", 4: "Closed"}
        df["event_type_label"] = df["event_type"].map(type_map).fillna(df["event_type"].astype(str))
        fig = px.histogram(df, x="event_type_label", title="Position Events")
        st.plotly_chart(fig, use_container_width=True)

    st.dataframe(df.tail(50), use_container_width=True)


def render_market_event(data: ArrowData):
    df = data.df
    if df is None or df.empty:
        st.info("No MarketEvent data.")
        return

    df = _add_time_index(df, "ts")
    if "symbol" in df.columns:
        symbols = sorted(df["symbol"].dropna().unique())
        symbol = st.selectbox("Symbol", symbols, key="market_symbol")
        df = df[df["symbol"] == symbol]

    if "close" in df.columns and "datetime" in df.columns:
        fig = px.line(df, x="datetime", y="close", title="Market Close")
        st.plotly_chart(fig, use_container_width=True)

    st.dataframe(df.tail(50), use_container_width=True)


def render_funding_event(data: ArrowData, position_data: ArrowData):
    df = data.df
    if df is None or df.empty:
        st.info("No FundingEvent data.")
        return

    df = _add_time_index(df, "ts")
    if "datetime" not in df.columns:
        st.info("FundingEvent has no usable timestamp column.")
        return

    symbol = None
    if "symbol" in df.columns:
        symbols = sorted(df["symbol"].dropna().unique())
        if symbols:
            symbol = st.selectbox("Funding Symbol", symbols, key="funding_symbol")
            df = df[df["symbol"] == symbol]

    if df.empty or "funding" not in df.columns:
        st.info("FundingEvent has no funding column.")
        return

    total_funding = df["funding"].sum()
    avg_funding = df["funding"].mean()
    event_count = len(df)

    cols = st.columns(3)
    cols[0].metric("Funding Events", f"{event_count:,}")
    cols[1].metric("Total Funding", f"{total_funding:,.2f}")
    cols[2].metric("Average Funding", f"{avg_funding:,.4f}")

    funding_ts = df[["datetime", "funding"]].copy()
    funding_ts = funding_ts.sort_values("datetime")
    funding_ts["cum_funding"] = funding_ts["funding"].cumsum()

    fig = px.line(
        funding_ts,
        x="datetime",
        y="cum_funding",
        title="Cumulative Funding",
    )
    st.plotly_chart(fig, use_container_width=True)

    daily = funding_ts.set_index("datetime")["funding"].resample("1D").sum().dropna()
    if not daily.empty:
        daily_df = daily.reset_index().rename(columns={"funding": "daily_funding"})
        daily_fig = px.bar(daily_df, x="datetime", y="daily_funding", title="Daily Funding")
        st.plotly_chart(daily_fig, use_container_width=True)

    # Funding vs exposure (signed notional proxy) when mark price and quantity exist.
    if {"quantity", "has_mark_price", "mark_price", "is_long"}.issubset(df.columns):
        exp_df = df[df["has_mark_price"]].copy()
        if not exp_df.empty:
            signed_qty = exp_df["quantity"].astype(float) * exp_df["is_long"].map({True: 1.0, False: -1.0})
            exp_df["signed_notional"] = signed_qty * exp_df["mark_price"].astype(float)

            scatter = px.scatter(
                exp_df,
                x="signed_notional",
                y="funding",
                hover_data=["datetime", "symbol", "rate", "mark_price", "quantity", "is_long"],
                title="Funding vs Signed Notional (Proxy Exposure)",
            )
            st.plotly_chart(scatter, use_container_width=True)

            exp_daily = exp_df.set_index("datetime")[["funding", "signed_notional"]].resample("1D").sum().dropna()
            if not exp_daily.empty:
                exp_daily = exp_daily.reset_index()
                exp_line = px.line(
                    exp_daily,
                    x="datetime",
                    y=["funding", "signed_notional"],
                    title="Daily Funding and Signed Notional",
                )
                st.plotly_chart(exp_line, use_container_width=True)

    # Exposure from PositionEvent (more explainable).
    pos_df = position_data.df
    if pos_df is not None and not pos_df.empty and "notional" in pos_df.columns and "is_long" in pos_df.columns:
        pos_df = _add_time_index(pos_df, "ts")
        if "datetime" in pos_df.columns:
            if symbol and "symbol" in pos_df.columns:
                pos_df = pos_df[pos_df["symbol"] == symbol]
            pos_df = pos_df.dropna(subset=["datetime"])
            if not pos_df.empty:
                pos_df["signed_notional"] = pos_df["notional"].astype(float) * pos_df["is_long"].map({True: 1.0, False: -1.0})
                exposure_ts = pos_df.set_index("datetime")["signed_notional"].sort_index()
                exposure_daily = exposure_ts.resample("1D").last().dropna()

                if not exposure_daily.empty:
                    exp_daily_df = exposure_daily.reset_index().rename(columns={"signed_notional": "daily_exposure"})
                    exp_fig = px.line(exp_daily_df, x="datetime", y="daily_exposure", title="Daily Exposure (PositionEvent)")
                    st.plotly_chart(exp_fig, use_container_width=True)

                    funding_daily = funding_ts.set_index("datetime")["funding"].resample("1D").sum().dropna()
                    merged = exp_daily_df.merge(
                        funding_daily.reset_index().rename(columns={"funding": "daily_funding"}),
                        on="datetime",
                        how="inner",
                    )
                    if not merged.empty:
                        merged["abs_exposure"] = merged["daily_exposure"].abs()
                        merged["funding_yield"] = merged.apply(
                            lambda r: (r["daily_funding"] / r["abs_exposure"]) if r["abs_exposure"] > 0 else None,
                            axis=1,
                        )
                        yield_fig = px.line(
                            merged,
                            x="datetime",
                            y="funding_yield",
                            title="Daily Funding Yield (Funding / |Exposure|)",
                        )
                        st.plotly_chart(yield_fig, use_container_width=True)

    # Spot/Perp alignment check (delta-neutral sanity check).
    if symbol and pos_df is not None and not pos_df.empty and "symbol" in pos_df.columns:
        spot_symbol = None
        perp_symbol = None
        if symbol.endswith("_PERP"):
            perp_symbol = symbol
            spot_symbol = symbol.replace("_PERP", "_SPOT")
        if spot_symbol and perp_symbol:
            spot_pos = position_data.df
            spot_pos = _add_time_index(spot_pos, "ts")
            spot_pos = spot_pos[(spot_pos["symbol"] == spot_symbol) & spot_pos["datetime"].notna()]
            perp_pos = position_data.df
            perp_pos = _add_time_index(perp_pos, "ts")
            perp_pos = perp_pos[(perp_pos["symbol"] == perp_symbol) & perp_pos["datetime"].notna()]
            if not spot_pos.empty and not perp_pos.empty:
                spot_pos["signed_notional"] = spot_pos["notional"].astype(float) * spot_pos["is_long"].map({True: 1.0, False: -1.0})
                perp_pos["signed_notional"] = perp_pos["notional"].astype(float) * perp_pos["is_long"].map({True: 1.0, False: -1.0})
                spot_daily = spot_pos.set_index("datetime")["signed_notional"].resample("1D").last().dropna()
                perp_daily = perp_pos.set_index("datetime")["signed_notional"].resample("1D").last().dropna()
                merged = pd.concat([spot_daily, perp_daily], axis=1, join="inner")
                merged.columns = ["spot_exposure", "perp_exposure"]
                if not merged.empty:
                    merged["net_exposure"] = merged["spot_exposure"] + merged["perp_exposure"]
                    merged["gross_exposure"] = merged["spot_exposure"].abs() + merged["perp_exposure"].abs()
                    merged["alignment_ratio"] = merged.apply(
                        lambda r: (abs(r["net_exposure"]) / r["gross_exposure"]) if r["gross_exposure"] > 0 else None,
                        axis=1,
                    )

                    st.subheader("Exposure Alignment (Spot vs Perp)")
                    cols = st.columns(3)
                    cols[0].metric("Avg |Net Exposure|", f"{merged['net_exposure'].abs().mean():,.2f}")
                    cols[1].metric("Avg Gross Exposure", f"{merged['gross_exposure'].mean():,.2f}")
                    cols[2].metric("Avg Alignment Ratio", f"{merged['alignment_ratio'].mean():.2%}")

                    align_df = merged.reset_index()
                    align_fig = px.line(
                        align_df,
                        x="datetime",
                        y=["spot_exposure", "perp_exposure", "net_exposure"],
                        title="Daily Exposure Alignment",
                    )
                    st.plotly_chart(align_fig, use_container_width=True)

    st.dataframe(df.tail(100), use_container_width=True)


def render_performance(account_evt: ArrowData):
    df = account_evt.df
    if df is None or df.empty:
        st.info("No AccountEvent data.")
        return

    equity_df = _compute_equity_df(df)
    if equity_df is None or equity_df.empty:
        st.info("AccountEvent has no usable equity data.")
        return

    if "equity_source" in equity_df.columns and not equity_df.empty:
        st.caption(f"Equity source: `{equity_df['equity_source'].iloc[0]}`")

    fig = px.line(equity_df, x="datetime", y="equity", title="Equity Curve")
    st.plotly_chart(fig, use_container_width=True)

    dd = _compute_drawdown(equity_df["equity"])
    dd_fig = px.line(x=equity_df["datetime"], y=dd, title="Drawdown")
    st.plotly_chart(dd_fig, use_container_width=True)

    cache_key = _cache_key_for(account_evt.path)
    metrics = _cached_perf_metrics(equity_df, cache_key)
    cols = st.columns(6)
    cols[0].metric("Total Return", f"{metrics['total_return']:.2%}" if metrics["total_return"] is not None else "n/a")
    cols[1].metric("CAGR", f"{metrics['cagr']:.2%}" if metrics["cagr"] is not None else "n/a")
    cols[2].metric("Max Drawdown", f"{metrics['max_drawdown']:.2%}" if metrics["max_drawdown"] is not None else "n/a")
    cols[3].metric("Volatility", f"{metrics['volatility']:.2%}" if metrics["volatility"] is not None else "n/a")
    cols[4].metric("Sharpe", f"{metrics['sharpe']:.2f}" if metrics["sharpe"] is not None else "n/a")
    cols[5].metric("Sortino", f"{metrics['sortino']:.2f}" if metrics["sortino"] is not None else "n/a")


def render_trade_stats(account_evt: ArrowData, order_evt: ArrowData):
    ae = account_evt.df
    oe = order_evt.df
    if ae is None or ae.empty:
        st.info("No AccountEvent data.")
        return

    ae = _add_time_index(ae, "ts")
    if "request_id" in ae.columns and "source_order_id" in ae.columns:
        fill_rows = ae[(ae["request_id"] > 0) & (ae["source_order_id"] >= 0)]
    else:
        fill_rows = ae.iloc[0:0]
    fill_rows = fill_rows.sort_values("datetime")
    acc_fills = fill_rows

    fee_sum = None
    fills = None
    if oe is not None and not oe.empty:
        oe = _add_time_index(oe, "ts")
        fills = oe[oe.get("event_type", -1) == 3].copy()
        if "fee" in fills.columns:
            fee_sum = fills["fee"].sum()

    cols = st.columns(4)
    cols[0].metric("Fill Count", f"{len(fill_rows)}")
    cols[1].metric("Total Fees", f"{fee_sum:,.2f}" if fee_sum is not None else "n/a")

    if "wallet_delta" in fill_rows.columns and not fill_rows.empty:
        wins = (fill_rows["wallet_delta"] > 0).sum()
        losses = (fill_rows["wallet_delta"] < 0).sum()
        total = wins + losses
        win_rate = wins / total if total > 0 else None
        avg_win = fill_rows[fill_rows["wallet_delta"] > 0]["wallet_delta"].mean()
        avg_loss = fill_rows[fill_rows["wallet_delta"] < 0]["wallet_delta"].mean()
        cols[2].metric("Win Rate (fill delta)", f"{win_rate:.2%}" if win_rate is not None else "n/a")
        if pd.notna(avg_win) and pd.notna(avg_loss):
            cols[3].metric("Avg Win / Loss", f"{avg_win:,.2f} / {avg_loss:,.2f}")
        else:
            cols[3].metric("Avg Win / Loss", "n/a")

        fig = px.histogram(fill_rows, x="wallet_delta", title="Wallet Delta per Fill")
        st.plotly_chart(fig, use_container_width=True)

        st.caption("Win rate uses wallet_delta per fill snapshot (fees included).")

    if not fill_rows.empty and "wallet_delta" in fill_rows.columns:
        breakdown = fill_rows.copy()
        if "ledger" in breakdown.columns:
            breakdown["ledger_label"] = breakdown["ledger"].map(ACCOUNT_LEDGER_MAP).fillna(
                breakdown["ledger"].astype(str)
            )
        else:
            breakdown["ledger_label"] = "Unknown"

        by_ledger = (
            breakdown.groupby("ledger_label", dropna=False)["wallet_delta"]
            .agg(["count", "sum", "mean"])
            .reset_index()
            .rename(columns={"count": "fills", "sum": "wallet_delta_sum", "mean": "wallet_delta_mean"})
        )
        by_ledger = by_ledger.sort_values("wallet_delta_sum", ascending=False)
        st.subheader("Ledger PnL Breakdown")
        st.dataframe(by_ledger, use_container_width=True)
        fig_ledger = px.bar(
            by_ledger,
            x="ledger_label",
            y="wallet_delta_sum",
            title="Wallet Delta by Ledger",
        )
        st.plotly_chart(fig_ledger, use_container_width=True)

    st.dataframe(fill_rows.tail(50), use_container_width=True)

    # Per-position realized PnL (excluding fees) using closing_position_id.
    if oe is not None and not oe.empty and "closing_position_id" in oe.columns:
        fills = oe[oe.get("event_type", -1) == 3].copy()
        if not fills.empty:
            fee_by_order = fills.groupby("order_id")["fee"].sum() if "fee" in fills.columns else pd.Series(dtype="float64")
            acc_by_order = acc_fills.groupby("source_order_id")["wallet_delta"].sum() if "wallet_delta" in acc_fills.columns else pd.Series(dtype="float64")
            joined = pd.concat([acc_by_order, fee_by_order], axis=1)
            joined.columns = ["wallet_delta_sum", "fee_sum"]
            joined = joined.fillna(0.0)
            joined["realized_ex_fee"] = joined["wallet_delta_sum"] + joined["fee_sum"]
            order_pos = fills[["order_id", "closing_position_id"]].dropna().drop_duplicates()
            pos_pnl = order_pos.merge(joined, left_on="order_id", right_index=True, how="left")
            pos_pnl = pos_pnl.groupby("closing_position_id")["realized_ex_fee"].sum().reset_index()
            st.subheader("Position PnL (Ex Fees)")
            st.metric("Total Realized PnL (Ex Fees)", f"{pos_pnl['realized_ex_fee'].sum():,.2f}")
            st.dataframe(pos_pnl.sort_values("realized_ex_fee").head(20), use_container_width=True)
            st.dataframe(pos_pnl.sort_values("realized_ex_fee", ascending=False).head(20), use_container_width=True)


def render_rolling_stats(account_evt: ArrowData):
    df = account_evt.df
    if df is None or df.empty:
        st.info("No AccountEvent data.")
        return

    equity_df = _compute_equity_df(df)
    if equity_df is None or equity_df.empty:
        st.info("AccountEvent has no usable equity data.")
        return

    cache_key = _cache_key_for(account_evt.path)
    daily = _cached_daily_equity(equity_df, cache_key)
    if daily is None or daily.empty:
        st.info("Not enough data for rolling stats.")
        return

    metric = st.selectbox("Rolling Metric", ["Average Return", "Sharpe", "Volatility", "Max Drawdown"])
    table = _cached_rolling_table(daily, cache_key, metric)
    if table is None or table.empty:
        st.info("No rolling stats available.")
        return

    st.dataframe(table.tail(24), use_container_width=True)


def render_benchmark_stats(account_evt: ArrowData, market_evt: ArrowData):
    ae = account_evt.df
    me = market_evt.df
    if ae is None or ae.empty or me is None or me.empty:
        st.info("Need AccountEvent and MarketEvent data for benchmark stats.")
        return

    equity_df = _compute_equity_df(ae)
    if equity_df is None or equity_df.empty:
        st.info("AccountEvent has no usable equity data.")
        return

    market_evt_df = _add_time_index(me, "ts")
    symbols = sorted(market_evt_df["symbol"].dropna().unique()) if "symbol" in market_evt_df.columns else []
    if not symbols:
        st.info("MarketEvent has no symbols.")
        return

    default_symbol = symbols[0]
    bench_symbol = st.selectbox("Benchmark Symbol", symbols, index=0, key="bench_symbol")
    cache_key_eq = _cache_key_for(account_evt.path)
    cache_key_mk = _cache_key_for(market_evt.path)
    metrics = _cached_benchmark_metrics(equity_df, market_evt_df, cache_key_eq, cache_key_mk, bench_symbol)

    daily_eq = _build_daily_equity(equity_df)
    daily_bm = _build_daily_benchmark(market_evt_df, bench_symbol)
    if daily_eq is None or daily_bm is None:
        st.info("Not enough data to build benchmark series.")
        return

    merged = daily_eq.join(daily_bm, how="inner")
    if merged.empty:
        st.info("No overlapping dates for strategy and benchmark.")
        return

    fig = px.line(merged.reset_index(), x="datetime", y=["equity", "bench_close"], title="Strategy vs Benchmark")
    st.plotly_chart(fig, use_container_width=True)

    cols = st.columns(3)
    cols[0].metric("Alpha (ann.)", f"{metrics['alpha']:.2%}" if metrics["alpha"] is not None else "n/a")
    cols[1].metric("Beta", f"{metrics['beta']:.2f}" if metrics["beta"] is not None else "n/a")
    cols[2].metric("Information Ratio", f"{metrics['info_ratio']:.2f}" if metrics["info_ratio"] is not None else "n/a")


def render_capacity_turnover(account_evt: ArrowData, order_evt: ArrowData, market_evt: ArrowData):
    ae = account_evt.df
    oe = order_evt.df
    me = market_evt.df
    if ae is None or ae.empty:
        st.info("No AccountEvent data.")
        return
    if oe is None or oe.empty:
        st.info("No OrderEvent data.")
        return
    if me is None or me.empty:
        st.info("No MarketEvent data.")
        return

    ae = _add_time_index(ae, "ts")
    equity_df = _compute_equity_df(ae)
    if equity_df is None or equity_df.empty:
        st.info("AccountEvent has no usable equity data.")
        return

    oe = _add_time_index(oe, "ts")
    fills = oe[oe.get("event_type", -1) == 3].copy()
    if fills.empty:
        st.info("No filled orders for turnover/capacity.")
        return

    cache_key_eq = _cache_key_for(account_evt.path)
    cache_key_ord = _cache_key_for(order_evt.path)
    cache_key_mk = _cache_key_for(market_evt.path)
    participation = st.slider("Participation Rate", min_value=0.1, max_value=5.0, value=1.0, step=0.1)
    participation_rate = participation / 100.0
    cache = _cached_capacity_turnover(
        fills,
        equity_df,
        me,
        cache_key_ord,
        cache_key_eq,
        cache_key_mk,
        participation_rate,
    )

    by_symbol = cache.get("by_symbol")
    if by_symbol is None or by_symbol.empty:
        st.info("Not enough data for capacity/turnover.")
        return

    st.subheader("Asset Sales Volume")
    fig = px.treemap(by_symbol, path=["symbol"], values="notional", title="Traded Notional by Symbol")
    st.plotly_chart(fig, use_container_width=True)

    st.subheader("Turnover")
    total_traded = cache.get("total_traded")
    avg_equity = cache.get("avg_equity")
    turnover = cache.get("turnover")
    cols = st.columns(3)
    cols[0].metric("Total Traded Notional", f"{total_traded:,.2f}" if total_traded is not None else "n/a")
    cols[1].metric("Average Equity", f"{avg_equity:,.2f}" if avg_equity else "n/a")
    cols[2].metric("Turnover", f"{turnover:.2f}x" if turnover is not None else "n/a")

    st.subheader("Capacity (Heuristic)")
    adv = cache.get("adv")
    total_capacity = cache.get("capacity")
    if adv is None or adv.empty:
        st.info("Not enough market data for capacity.")
        return
    st.metric("Estimated Strategy Capacity (sum of ADV * participation)", f"{total_capacity:,.2f}" if total_capacity is not None else "n/a")
    st.dataframe(adv.sort_values("capacity", ascending=False), use_container_width=True)


def render_order_details(order_evt: ArrowData):
    df = order_evt.df
    if df is None or df.empty:
        st.info("No OrderEvent data.")
        return

    df = _add_time_index(df, "ts")
    type_map = {0: "Accepted", 1: "Rejected", 2: "Canceled", 3: "Filled"}
    side_map = {0: "Buy", 1: "Sell"}
    pos_side_map = {0: "Both", 1: "Long", 2: "Short"}

    df["event_type_label"] = df.get("event_type", pd.Series()).map(type_map).fillna(df.get("event_type"))
    df["side_label"] = df.get("side", pd.Series()).map(side_map).fillna(df.get("side"))
    df["position_side_label"] = df.get("position_side", pd.Series()).map(pos_side_map).fillna(df.get("position_side"))

    only_fills = st.checkbox("Show only fills", value=True)
    if only_fills:
        df = df[df.get("event_type", -1) == 3]

    if "exec_qty" in df.columns and "exec_price" in df.columns:
        df["notional"] = df["exec_qty"].fillna(0.0) * df["exec_price"].fillna(0.0)

    symbols = ["All"]
    if "symbol" in df.columns:
        symbols += sorted(df["symbol"].dropna().unique().tolist())
    symbol = st.selectbox("Symbol", symbols, index=0, key="order_details_symbol")
    if symbol != "All":
        df = df[df["symbol"] == symbol]

    cols = st.columns(3)
    cols[0].metric("Rows", f"{len(df)}")
    if "fee" in df.columns:
        cols[1].metric("Total Fees", f"{df['fee'].sum():,.2f}")
    if "notional" in df.columns:
        cols[2].metric("Total Notional", f"{df['notional'].sum():,.2f}")

    show_cols = [
        "datetime",
        "symbol",
        "event_type_label",
        "side_label",
        "position_side_label",
        "reduce_only",
        "qty",
        "price",
        "exec_qty",
        "exec_price",
        "remaining_qty",
        "is_taker",
        "fee",
        "fee_rate",
    ]
    existing = [c for c in show_cols if c in df.columns]
    st.dataframe(df[existing].sort_values("datetime").tail(200), use_container_width=True)


def render_overview(account_evt: ArrowData, order_evt: ArrowData, market_evt: ArrowData, funding_evt: ArrowData):
    ae = account_evt.df
    oe = order_evt.df
    me = market_evt.df
    fe = funding_evt.df
    if ae is None or ae.empty:
        st.info("No AccountEvent data.")
        return

    ae = _add_time_index(ae, "ts")
    equity_df = _compute_equity_df(ae)
    if equity_df is None or equity_df.empty:
        st.info("AccountEvent has no usable equity data.")
        return

    cache_key_eq = _cache_key_for(account_evt.path)
    perf = _cached_perf_metrics(equity_df, cache_key_eq)
    drawdown_recovery = _cached_drawdown_recovery(equity_df, cache_key_eq)

    start_equity = equity_df["equity"].iloc[0]
    end_equity = equity_df["equity"].iloc[-1]
    net_profit = end_equity - start_equity
    total_return = perf["total_return"]

    # Trade metrics from fill snapshots.
    fill_rows = ae[(ae.get("request_id", 0) > 0) & (ae.get("source_order_id", -1) >= 0)] if "request_id" in ae.columns else ae.iloc[0:0]
    wins = fill_rows[fill_rows.get("wallet_delta", 0) > 0] if "wallet_delta" in fill_rows.columns else fill_rows.iloc[0:0]
    losses = fill_rows[fill_rows.get("wallet_delta", 0) < 0] if "wallet_delta" in fill_rows.columns else fill_rows.iloc[0:0]
    total_fills = len(fill_rows)
    win_rate = (len(wins) / total_fills) if total_fills > 0 else None
    avg_win = wins["wallet_delta"].mean() if not wins.empty else None
    avg_loss = losses["wallet_delta"].mean() if not losses.empty else None

    wallet_delta_sum = ae["wallet_delta"].sum() if "wallet_delta" in ae.columns else None

    total_fees = None
    if oe is not None and not oe.empty and "fee" in oe.columns and "event_type" in oe.columns:
        total_fees = oe[oe["event_type"] == 3]["fee"].sum()

    funding_sum = None
    if fe is not None and not fe.empty and "funding" in fe.columns:
        funding_sum = fe["funding"].sum()

    residual_price_pnl = None
    if wallet_delta_sum is not None and funding_sum is not None and total_fees is not None:
        residual_price_pnl = wallet_delta_sum - funding_sum - total_fees

    overview = [
        ("Total Orders", f"{total_fills:,}"),
        ("Win Rate", _format_pct(win_rate)),
        ("Start Equity", _format_num(start_equity)),
        ("End Equity", _format_num(end_equity)),
        ("Net Profit", _format_num(net_profit)),
        ("Total Return", _format_pct(total_return)),
        ("CAGR", _format_pct(perf["cagr"])),
        ("Max Drawdown", _format_pct(perf["max_drawdown"])),
        ("Sharpe", _format_ratio(perf["sharpe"])),
        ("Total Fees", _format_num(total_fees)),
        ("Funding Sum", _format_num(funding_sum)),
        ("Residual Price/Basis", _format_num(residual_price_pnl)),
        ("Avg Win (fill delta)", _format_num(avg_win)),
        ("Avg Loss (fill delta)", _format_num(avg_loss)),
        ("Drawdown Recovery (days)", f"{drawdown_recovery}" if drawdown_recovery is not None else "n/a"),
    ]

    left = overview[::2]
    right = overview[1::2]
    table_rows = []
    for i in range(max(len(left), len(right))):
        l = left[i] if i < len(left) else ("", "")
        r = right[i] if i < len(right) else ("", "")
        table_rows.append({
            "Metric": l[0],
            "Value": l[1],
            "Metric ": r[0],
            "Value ": r[1],
        })
    st.dataframe(pd.DataFrame(table_rows), use_container_width=True)

    ledger_mix = _build_ledger_mix_df(ae)
    if ledger_mix is None or ledger_mix.empty:
        return

    st.subheader("Dual-Ledger KPIs")
    end_row = ledger_mix.iloc[-1]
    cols = st.columns(6)
    cols[0].metric("End Spot Ledger", _format_num(float(end_row["spot_ledger"])))
    cols[1].metric("End Perp Ledger", _format_num(float(end_row["perp_ledger"])))
    cols[2].metric("End Total Ledger", _format_num(float(end_row["total_ledger"])))
    cols[3].metric("End Spot Ratio", _format_pct(float(end_row["spot_ratio"])))
    cols[4].metric("Avg Spot Ratio", _format_pct(float(ledger_mix["spot_ratio"].mean())))
    cols[5].metric("Spot Ratio Vol", _format_pct(float(ledger_mix["spot_ratio"].std())))

    fig_ledger = px.line(
        ledger_mix,
        x="datetime",
        y=["spot_ledger", "perp_ledger", "total_ledger"],
        title="Ledger Value Split",
    )
    st.plotly_chart(fig_ledger, use_container_width=True)

    fig_ratio = px.line(
        ledger_mix,
        x="datetime",
        y=["spot_ratio", "perp_ratio"],
        title="Ledger Ratio (Spot vs Perp)",
    )
    st.plotly_chart(fig_ratio, use_container_width=True)


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--logs", default=None)
    args, _ = parser.parse_known_args()

    st.set_page_config(page_title="QTrading Report", layout="wide")
    st.title("QTrading Report")

    default_logs = args.logs or _parse_cli_logs_dir() or os.path.abspath(os.path.join(os.getcwd(), "..", "..", "logs"))
    logs_root = st.sidebar.text_input("Logs directory", value=default_logs)

    if not os.path.isdir(logs_root):
        st.warning("Logs directory does not exist.")
        return

    run_dirs = _list_run_dirs(logs_root)
    if run_dirs:
        selected_run = st.sidebar.selectbox("Log run", run_dirs, index=0)
        if st.session_state.get("_last_log_run") != selected_run:
            st.cache_data.clear()
            st.session_state["_last_log_run"] = selected_run
        logs_dir = os.path.join(logs_root, selected_run)
    else:
        logs_dir = logs_root

    run_meta_json = _load_run_metadata_json(logs_dir)
    if run_meta_json:
        st.sidebar.subheader("Run Metadata (JSON)")
        st.sidebar.json(run_meta_json)

    dataset_meta = _load_dataset_paths_json(logs_dir)
    if dataset_meta:
        st.sidebar.subheader("Dataset Paths (JSON)")
        st.sidebar.json(dataset_meta)

    arrow_data = _load_arrow_files(logs_dir)
    if not arrow_data:
        st.warning("No .arrow files found.")
        return

    st.subheader("Summary")
    render_summary(arrow_data)

    tabs = st.tabs([
        "Overview",
        "Performance",
        "Trades",
        "Funding",
        "Rolling",
        "Benchmark",
        "Capacity",
        "AccountEvent",
        "OrderEvent",
        "PositionEvent",
        "MarketEvent",
        "Files"
    ])

    with tabs[0]:
        render_overview(
            arrow_data.get("AccountEvent.arrow", ArrowData("", "", 0, None)),
            arrow_data.get("OrderEvent.arrow", ArrowData("", "", 0, None)),
            arrow_data.get("MarketEvent.arrow", ArrowData("", "", 0, None)),
            arrow_data.get("FundingEvent.arrow", ArrowData("", "", 0, None)),
        )

    with tabs[1]:
        render_performance(arrow_data.get("AccountEvent.arrow", ArrowData("", "", 0, None)))

    with tabs[2]:
        render_trade_stats(
            arrow_data.get("AccountEvent.arrow", ArrowData("", "", 0, None)),
            arrow_data.get("OrderEvent.arrow", ArrowData("", "", 0, None)),
        )

    with tabs[3]:
        render_funding_event(
            arrow_data.get("FundingEvent.arrow", ArrowData("", "", 0, None)),
            arrow_data.get("PositionEvent.arrow", ArrowData("", "", 0, None)),
        )

    with tabs[4]:
        render_rolling_stats(arrow_data.get("AccountEvent.arrow", ArrowData("", "", 0, None)))

    with tabs[5]:
        render_benchmark_stats(
            arrow_data.get("AccountEvent.arrow", ArrowData("", "", 0, None)),
            arrow_data.get("MarketEvent.arrow", ArrowData("", "", 0, None)),
        )

    with tabs[6]:
        render_capacity_turnover(
            arrow_data.get("AccountEvent.arrow", ArrowData("", "", 0, None)),
            arrow_data.get("OrderEvent.arrow", ArrowData("", "", 0, None)),
            arrow_data.get("MarketEvent.arrow", ArrowData("", "", 0, None)),
        )

    with tabs[7]:
        render_account_event(arrow_data.get("AccountEvent.arrow", ArrowData("", "", 0, None)))

    with tabs[8]:
        render_order_event(arrow_data.get("OrderEvent.arrow", ArrowData("", "", 0, None)))

    with tabs[9]:
        render_position_event(arrow_data.get("PositionEvent.arrow", ArrowData("", "", 0, None)))

    with tabs[10]:
        render_market_event(arrow_data.get("MarketEvent.arrow", ArrowData("", "", 0, None)))

    with tabs[11]:
        rows = []
        for item in arrow_data.values():
            rows.append({
                "file": item.name,
                "rows": item.rows,
                "size_bytes": os.path.getsize(item.path),
                "path": item.path,
            })
        st.dataframe(pd.DataFrame(rows).sort_values("file"), use_container_width=True)


if __name__ == "__main__":
    main()
