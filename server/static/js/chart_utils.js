/** Shared Chart.js options — axis labels, units, tooltips. */
const VW_CHART = (() => {
    const C = window.VW_CENTRAL || {};

    const kinds = {
        v: { axis: () => C.chart_y_voltage || 'Voltage (V)', unit: 'V', decimals: 1 },
        i: { axis: () => C.chart_y_current || 'Current (A)', unit: 'A', decimals: 3 },
        p: { axis: () => C.chart_y_power || 'Power (W)', unit: 'W', decimals: 1 },
    };

    function theme() {
        const dark = document.documentElement.getAttribute('data-theme') !== 'light';
        return {
            grid: dark ? '#1f1f2e' : '#e4e4e7',
            text: dark ? '#6b7280' : '#71717a',
            accent: dark ? '#22d3ee' : '#0891b2',
        };
    }

    function cfg(kind) {
        return kinds[kind] || kinds.p;
    }

    function fmtValue(value, kind) {
        const c = cfg(kind);
        if (value == null || Number.isNaN(value)) return '—';
        return `${fmtNum(value, c.decimals)} ${c.unit}`;
    }

    function yScale(kind, showY) {
        const t = theme();
        const c = cfg(kind);
        return {
            display: showY,
            grid: { color: t.grid },
            ticks: {
                color: t.text,
                font: { size: 10, family: 'JetBrains Mono' },
                maxTicksLimit: 5,
                callback: (v) => `${fmtNum(v, c.decimals)} ${c.unit}`,
            },
            title: {
                display: showY,
                text: c.axis(),
                color: t.text,
                font: { size: 10, weight: '600' },
                padding: { top: 0, bottom: 6 },
            },
        };
    }

    function timeLabels(timestamps) {
        return (timestamps || []).map(ts => fmtTime(ts * 1000));
    }

    function xTimeScale(show = true) {
        const t = theme();
        return {
            display: show,
            grid: { color: t.grid },
            ticks: {
                color: t.text,
                font: { size: 10 },
                maxTicksLimit: 6,
                maxRotation: 0,
                autoSkip: true,
            },
            title: {
                display: show,
                text: C.chart_x_time || 'Time',
                color: t.text,
                font: { size: 10, weight: '600' },
                padding: { top: 4 },
            },
        };
    }

    function tooltipCallbacks(kind) {
        const c = cfg(kind);
        return {
            label(ctx) {
                return `${ctx.dataset.label}: ${fmtValue(ctx.parsed.y, kind)}`;
            },
        };
    }

    function trendOptions(kind, showY = true) {
        const t = theme();
        return {
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            plugins: {
                legend: {
                    display: true,
                    position: 'top',
                    align: 'end',
                    labels: { boxWidth: 8, font: { size: 10 }, color: t.text },
                },
                tooltip: {
                    mode: 'index',
                    intersect: false,
                    callbacks: {
                        title: (items) => items[0]?.label || '',
                        ...tooltipCallbacks(kind),
                    },
                },
            },
            scales: {
                x: xTimeScale(true),
                y: yScale(kind, showY),
            },
            interaction: { mode: 'nearest', axis: 'x', intersect: false },
        };
    }

    function seriesOptions(kind, opts = {}) {
        const t = theme();
        const showX = opts.showX !== false;
        const showLegend = opts.showLegend === true;
        const c = cfg(kind);
        return {
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            plugins: {
                legend: { display: showLegend, labels: { color: t.text, font: { size: 11 } } },
                tooltip: {
                    mode: 'index',
                    intersect: false,
                    callbacks: {
                        title: (items) => items[0]?.label || '',
                        label(ctx) {
                            return `${ctx.dataset.label || c.axis()}: ${fmtValue(ctx.parsed.y, kind)}`;
                        },
                    },
                },
            },
            scales: {
                x: xTimeScale(showX),
                y: yScale(kind, true),
            },
        };
    }

    return { theme, trendOptions, seriesOptions, fmtValue, cfg, timeLabels };
})();
