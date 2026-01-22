import argparse
import os
import sys
from dataclasses import dataclass
from typing import Dict, Optional, Tuple

import pandas as pd
import plotly.express as px
import pyarrow as pa
import streamlit as st


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


def _compute_equity_df(account_evt: pd.DataFrame) -> Optional[pd.DataFrame]:
    if account_evt is None or account_evt.empty:
        return None

    df = account_evt.copy()
    if "ts" not in df.columns:
        return None

    df = _add_time_index(df, "ts")
    df = df[df["datetime"].notna()].sort_values("datetime")

    equity_col = None
    if "margin_balance_after" in df.columns:
        equity_col = "margin_balance_after"
    elif "wallet_balance_after" in df.columns:
        equity_col = "wallet_balance_after"
    if equity_col is None:
        return None

    df["equity"] = df[equity_col]
    df["is_fill_snapshot"] = (df.get("request_id", 0) > 0)
    return df


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

    start_balance = None
    end_balance = None
    wallet_delta_sum = None
    fee_sum = None

    if account_evt and account_evt.df is not None and not account_evt.df.empty:
        df = account_evt.df
        if "wallet_balance_after" in df.columns:
            start_balance = df["wallet_balance_after"].iloc[0]
            end_balance = df["wallet_balance_after"].iloc[-1]
        if "wallet_delta" in df.columns:
            wallet_delta_sum = df["wallet_delta"].sum()

    if order_evt and order_evt.df is not None and not order_evt.df.empty:
        df = order_evt.df
        if "fee" in df.columns:
            fee_sum = df["fee"].sum()

    cols = st.columns(4)
    cols[0].metric("Start Balance", f"{start_balance:,.2f}" if start_balance is not None else "n/a")
    cols[1].metric("End Balance", f"{end_balance:,.2f}" if end_balance is not None else "n/a")
    cols[2].metric("Wallet Delta Sum", f"{wallet_delta_sum:,.2f}" if wallet_delta_sum is not None else "n/a")
    cols[3].metric("Total Fees", f"{fee_sum:,.2f}" if fee_sum is not None else "n/a")


def render_account_event(data: ArrowData):
    df = data.df
    if df is None or df.empty:
        st.info("No AccountEvent data.")
        return

    df = _add_time_index(df, "ts")

    chart_cols = []
    for col in ["wallet_balance_after", "margin_balance_after", "available_balance_after"]:
        if col in df.columns:
            chart_cols.append(col)
    if not chart_cols:
        st.info("AccountEvent has no balance columns.")
        return

    fig = px.line(df, x="datetime", y=chart_cols, title="Account Balances")
    st.plotly_chart(fig, use_container_width=True)

    st.dataframe(df.tail(50), use_container_width=True)


def render_account_log(data: ArrowData):
    df = data.df
    if df is None or df.empty:
        st.info("No AccountLog data.")
        return

    df = _add_time_index(df, "timestamp")
    y_cols = [c for c in ["balance", "unreal_pnl", "equity"] if c in df.columns]
    if not y_cols:
        st.info("AccountLog has no balance columns.")
        return

    fig = px.line(df, x="datetime", y=y_cols, title="Account Log")
    st.plotly_chart(fig, use_container_width=True)
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


def render_performance(account_evt: ArrowData):
    df = account_evt.df
    if df is None or df.empty:
        st.info("No AccountEvent data.")
        return

    equity_df = _compute_equity_df(df)
    if equity_df is None or equity_df.empty:
        st.info("AccountEvent has no usable equity data.")
        return

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


def render_overview(account_evt: ArrowData, order_evt: ArrowData, market_evt: ArrowData):
    ae = account_evt.df
    oe = order_evt.df
    me = market_evt.df
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

    # Trade metrics from fill snapshots.
    fill_rows = ae[(ae.get("request_id", 0) > 0) & (ae.get("source_order_id", -1) >= 0)] if "request_id" in ae.columns else ae.iloc[0:0]
    wins = fill_rows[fill_rows.get("wallet_delta", 0) > 0] if "wallet_delta" in fill_rows.columns else fill_rows.iloc[0:0]
    losses = fill_rows[fill_rows.get("wallet_delta", 0) < 0] if "wallet_delta" in fill_rows.columns else fill_rows.iloc[0:0]
    total_fills = len(fill_rows)
    win_rate = (len(wins) / total_fills) if total_fills > 0 else None
    loss_rate = (len(losses) / total_fills) if total_fills > 0 else None
    avg_win = wins["wallet_delta"].mean() if not wins.empty else None
    avg_loss = losses["wallet_delta"].mean() if not losses.empty else None
    pl_ratio = (avg_win / abs(avg_loss)) if avg_win is not None and avg_loss is not None and avg_loss != 0 else None
    expectancy = ((win_rate or 0) * (avg_win or 0)) + ((loss_rate or 0) * (avg_loss or 0)) if total_fills > 0 else None

    total_fees = None
    if oe is not None and not oe.empty and "fee" in oe.columns and "event_type" in oe.columns:
        total_fees = oe[oe["event_type"] == 3]["fee"].sum()

    # Benchmark metrics.
    alpha = beta = info_ratio = tracking_error = treynor = None
    if me is not None and not me.empty and "symbol" in me.columns:
        market_evt_df = _add_time_index(me, "ts")
        symbols = sorted(market_evt_df["symbol"].dropna().unique())
        if symbols:
            bench = symbols[0]
            cache_key_mk = _cache_key_for(market_evt.path)
            bm_metrics = _cached_benchmark_metrics(equity_df, market_evt_df, cache_key_eq, cache_key_mk, bench)
            alpha = bm_metrics["alpha"]
            beta = bm_metrics["beta"]
            info_ratio = bm_metrics["info_ratio"]
            tracking_error = bm_metrics["tracking_error"]
            treynor = bm_metrics["treynor"]

    # Capacity/turnover.
    turnover = capacity = None
    lowest_capacity_asset = None
    if oe is not None and not oe.empty and me is not None and not me.empty:
        fills = oe[oe.get("event_type", -1) == 3].copy()
        cache_key_ord = _cache_key_for(order_evt.path)
        cache_key_mk = _cache_key_for(market_evt.path)
        cap = _cached_capacity_turnover(
            fills,
            equity_df,
            me,
            cache_key_ord,
            cache_key_eq,
            cache_key_mk,
            0.01,
        )
        turnover = cap.get("turnover")
        capacity = cap.get("capacity")
        lowest_capacity_asset = cap.get("lowest_capacity_asset")

    overview = [
        ("Total Orders", f"{total_fills:,}"),
        ("Average Win", _format_num(avg_win)),
        ("Average Loss", _format_num(avg_loss)),
        ("Win Rate", _format_pct(win_rate)),
        ("Loss Rate", _format_pct(loss_rate)),
        ("Profit-Loss Ratio", _format_ratio(pl_ratio)),
        ("Expectancy", _format_num(expectancy)),
        ("Start Equity", _format_num(start_equity)),
        ("End Equity", _format_num(end_equity)),
        ("Net Profit", _format_num(net_profit)),
        ("Compounding Annual Return", _format_pct(perf["cagr"])),
        ("Drawdown", _format_pct(perf["max_drawdown"])),
        ("Drawdown Recovery (days)", f"{drawdown_recovery}" if drawdown_recovery is not None else "n/a"),
        ("Sharpe Ratio", _format_ratio(perf["sharpe"])),
        ("Sortino Ratio", _format_ratio(perf["sortino"])),
        ("Annual Standard Deviation", _format_pct(perf["volatility"])),
        ("Annual Variance", _format_ratio((perf["volatility"] or 0) ** 2) if perf["volatility"] is not None else "n/a"),
        ("Alpha", _format_pct(alpha)),
        ("Beta", _format_ratio(beta)),
        ("Information Ratio", _format_ratio(info_ratio)),
        ("Tracking Error", _format_ratio(tracking_error)),
        ("Treynor Ratio", _format_ratio(treynor)),
        ("Total Fees", _format_num(total_fees)),
        ("Estimated Strategy Capacity", _format_num(capacity)),
        ("Lowest Capacity Asset", str(lowest_capacity_asset) if lowest_capacity_asset is not None else "n/a"),
        ("Portfolio Turnover", _format_ratio(turnover)),
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


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--logs", default=None)
    args, _ = parser.parse_known_args()

    st.set_page_config(page_title="QTrading Report", layout="wide")
    st.title("QTrading Report")

    default_logs = args.logs or _parse_cli_logs_dir() or os.path.abspath(os.path.join(os.getcwd(), "..", "..", "logs"))
    logs_dir = st.sidebar.text_input("Logs directory", value=default_logs)

    if not os.path.isdir(logs_dir):
        st.warning("Logs directory does not exist.")
        return

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
        "Rolling",
        "Benchmark",
        "Capacity",
        "Order Details",
        "AccountEvent",
        "AccountLog",
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
        )

    with tabs[1]:
        render_performance(arrow_data.get("AccountEvent.arrow", ArrowData("", "", 0, None)))

    with tabs[2]:
        render_trade_stats(
            arrow_data.get("AccountEvent.arrow", ArrowData("", "", 0, None)),
            arrow_data.get("OrderEvent.arrow", ArrowData("", "", 0, None)),
        )

    with tabs[3]:
        render_rolling_stats(arrow_data.get("AccountEvent.arrow", ArrowData("", "", 0, None)))

    with tabs[4]:
        render_benchmark_stats(
            arrow_data.get("AccountEvent.arrow", ArrowData("", "", 0, None)),
            arrow_data.get("MarketEvent.arrow", ArrowData("", "", 0, None)),
        )

    with tabs[5]:
        render_capacity_turnover(
            arrow_data.get("AccountEvent.arrow", ArrowData("", "", 0, None)),
            arrow_data.get("OrderEvent.arrow", ArrowData("", "", 0, None)),
            arrow_data.get("MarketEvent.arrow", ArrowData("", "", 0, None)),
        )

    with tabs[6]:
        render_order_details(arrow_data.get("OrderEvent.arrow", ArrowData("", "", 0, None)))

    with tabs[7]:
        render_account_event(arrow_data.get("AccountEvent.arrow", ArrowData("", "", 0, None)))

    with tabs[8]:
        render_account_log(arrow_data.get("Account.arrow", ArrowData("", "", 0, None)))

    with tabs[9]:
        render_order_event(arrow_data.get("OrderEvent.arrow", ArrowData("", "", 0, None)))

    with tabs[10]:
        render_position_event(arrow_data.get("PositionEvent.arrow", ArrowData("", "", 0, None)))

    with tabs[11]:
        render_market_event(arrow_data.get("MarketEvent.arrow", ArrowData("", "", 0, None)))

    with tabs[12]:
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
