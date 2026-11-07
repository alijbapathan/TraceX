/**
 * TraceX Analysis Report Controller
 */

let currentRunId = '';
let currentStrategy = 'rule';
let currentAnalysis = null;
let comparisonChart = null;

document.addEventListener('DOMContentLoaded', async () => {
    initChart();
    setupEventListeners();

    await API.initRunSelector(async (runId) => {
        currentRunId = runId;
        await loadReport();
        await loadComparison();
    });
});

function initChart() {
    const ctx = document.getElementById('comparisonChart').getContext('2d');
    comparisonChart = new Chart(ctx, {
        type: 'bar',
        data: {
            labels: ['Rule-Based', 'Time-Window'],
            datasets: [
                {
                    label: 'Execution Time (ms)',
                    data: [0, 0],
                    backgroundColor: 'rgba(56, 189, 248, 0.7)',
                    borderColor: '#38bdf8',
                    borderWidth: 1,
                    yAxisID: 'y'
                },
                {
                    label: 'Confidence Score (%)',
                    data: [0, 0],
                    backgroundColor: 'rgba(239, 68, 68, 0.7)',
                    borderColor: '#ef4444',
                    borderWidth: 1,
                    yAxisID: 'y1'
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: { labels: { color: '#94a3b8', font: { size: 11 } } }
            },
            scales: {
                x: {
                    grid: { color: '#1e293b' },
                    ticks: { color: '#94a3b8' }
                },
                y: {
                    type: 'linear',
                    display: true,
                    position: 'left',
                    title: { display: true, text: 'Time (ms)', color: '#38bdf8' },
                    grid: { color: '#1e293b' },
                    ticks: { color: '#94a3b8' },
                    beginAtZero: true
                },
                y1: {
                    type: 'linear',
                    display: true,
                    position: 'right',
                    title: { display: true, text: 'Confidence (%)', color: '#ef4444' },
                    grid: { drawOnChartArea: false },
                    ticks: { color: '#94a3b8' },
                    min: 0,
                    max: 100
                }
            }
        }
    });
}

async function loadReport() {
    if (!currentRunId) return;

    try {
        currentAnalysis = await API.analyze(currentRunId, currentStrategy);
        const [logsData] = await Promise.all([
            API.getLogs(currentRunId, { limit: 1000 })
        ]);

        const rawEvents = logsData.events || [];
        const eventMap = new Map(rawEvents.map(e => [e.id, e]));

        // 1. Hypothesis Banner
        document.getElementById('report-root-cause').textContent =
            `${currentAnalysis.root_cause_type} (#${currentAnalysis.root_event_id})`;
        document.getElementById('report-explanation').textContent = currentAnalysis.explanation;
        document.getElementById('confidence-value').textContent = `${Math.round(currentAnalysis.confidence)}%`;
        document.getElementById('confidence-bar').style.width = `${Math.min(100, Math.max(5, currentAnalysis.confidence))}%`;
        document.getElementById('exec-time-pill').textContent = `${currentAnalysis.exec_ms.toFixed(3)} ms`;

        // 2. Correlated Failure Chains
        const chainContainer = document.getElementById('chain-container');
        chainContainer.innerHTML = '';
        const chains = currentAnalysis.chains || [];
        document.getElementById('chain-count-badge').textContent = `${chains.length} Chains`;

        if (chains.length === 0) {
            chainContainer.innerHTML = '<div style="color: var(--text-muted); font-size: 0.85rem;">No cascading failure chains detected.</div>';
        } else {
            chains.forEach((chain, idx) => {
                const chainDiv = document.createElement('div');
                chainDiv.style.cssText = 'background: var(--bg-dark); padding: 0.75rem; border-radius: var(--radius-sm); margin-bottom: 0.5rem; display: flex; flex-wrap: wrap; gap: 0.5rem; align-items: center; font-size: 0.85rem;';
                
                let html = `<span style="font-weight: 700; color: var(--text-muted);">Chain #${idx + 1}:</span> `;
                chain.forEach((evId, cIdx) => {
                    const ev = eventMap.get(evId);
                    const isRoot = (evId === currentAnalysis.root_event_id);
                    const badgeClass = isRoot ? 'badge-root' : 'badge-chain';
                    const src = ev ? ev.source : 'EVENT';

                    html += `<span class="badge ${badgeClass}" title="${ev ? ev.message : ''}">#${evId} ${src}</span>`;
                    if (cIdx < chain.length - 1) {
                        html += `<span style="color: var(--text-muted); font-size: 0.75rem;">&rarr;</span>`;
                    }
                });

                chainDiv.innerHTML = html;
                chainContainer.appendChild(chainDiv);
            });
        }

        // 3. Supporting Evidence
        const supportingTbody = document.getElementById('supporting-tbody');
        supportingTbody.innerHTML = '';
        const supIds = currentAnalysis.supporting_event_ids || [];
        document.getElementById('evidence-count-badge').textContent = `${supIds.length} Events`;

        if (supIds.length === 0) {
            supportingTbody.innerHTML = '<tr><td colspan="4" style="text-align: center; color: var(--text-muted);">No downstream supporting events.</td></tr>';
        } else {
            supIds.forEach(id => {
                const ev = eventMap.get(id);
                const tr = document.createElement('tr');
                tr.innerHTML = `
                    <td class="mono" style="color: var(--text-muted); font-weight:600;">#${id}</td>
                    <td class="mono" style="font-size: 0.8rem;">${ev ? ev.ts : '--'}</td>
                    <td><span class="mono" style="color: var(--accent-blue);">${ev ? ev.source : '--'}</span></td>
                    <td style="color: #fca5a5; font-size: 0.8rem;">${ev ? escapeHtml(ev.message) : '--'}</td>
                `;
                supportingTbody.appendChild(tr);
            });
        }

        // 4. Populate What-If Checkboxes
        populateWhatIfCheckboxes(rawEvents, currentAnalysis);

    } catch (err) {
        console.error('Error loading report:', err);
    }
}

function populateWhatIfCheckboxes(events, analysis) {
    const container = document.getElementById('whatif-checkboxes');
    container.innerHTML = '';

    // Filter to errors and candidate events
    const criticalEvents = events.filter(e => e.level === 'ERROR' || e.level === 'FATAL' || e.id === analysis.root_event_id);

    if (criticalEvents.length === 0) {
        container.innerHTML = '<div style="color: var(--text-muted); font-size: 0.8rem;">No critical error events available to suppress.</div>';
        return;
    }

    criticalEvents.forEach(e => {
        const isRoot = (e.id === analysis.root_event_id);
        const div = document.createElement('div');
        div.style.cssText = 'display: flex; align-items: center; gap: 0.5rem; margin-bottom: 0.35rem; font-size: 0.8rem;';
        
        div.innerHTML = `
            <input type="checkbox" id="whatif-ev-${e.id}" value="${e.id}" ${isRoot ? 'checked' : ''} style="cursor: pointer;">
            <label for="whatif-ev-${e.id}" style="cursor: pointer; display: flex; align-items: center; gap: 0.4rem;">
                <span class="mono" style="color: ${isRoot ? '#ef4444' : 'var(--text-muted)'}; font-weight: 700;">#${e.id}</span>
                <span class="badge ${e.level === 'FATAL' ? 'badge-fatal' : 'badge-error'}">${e.level}</span>
                <span style="color: var(--text-secondary);">${e.source}:</span>
                <span style="color: var(--text-primary); max-width: 320px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;">${escapeHtml(e.message)}</span>
            </label>
        `;
        container.appendChild(div);
    });
}

async function loadComparison() {
    if (!currentRunId) return;

    try {
        const comp = await API.compare(currentRunId);
        const tbody = document.getElementById('comparison-tbody');
        tbody.innerHTML = '';

        const comparisons = comp.comparisons || [];
        const times = [];
        const confidences = [];
        const labels = [];

        comparisons.forEach(item => {
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td style="font-weight: 600; color: var(--text-primary);">${item.strategy}</td>
                <td><span class="badge badge-root">${item.root_cause} (#${item.root_event_id})</span></td>
                <td style="font-weight: 700; color: var(--accent-red);">${item.confidence}%</td>
                <td>${item.chains_count} chains (${item.supporting_count} sup)</td>
                <td class="mono" style="color: var(--accent-blue);">${item.exec_ms.toFixed(3)} ms</td>
            `;
            tbody.appendChild(tr);

            labels.push(item.strategy.includes('Rule') ? 'Rule-Based' : 'Time-Window');
            times.push(item.exec_ms);
            confidences.push(item.confidence);
        });

        document.getElementById('comparison-summary').textContent = comp.summary || '';

        // Update Comparison Bar Chart
        comparisonChart.data.labels = labels;
        comparisonChart.data.datasets[0].data = times;
        comparisonChart.data.datasets[1].data = confidences;
        comparisonChart.update();

    } catch (err) {
        console.error('Error loading comparison:', err);
    }
}

function setupEventListeners() {
    // Strategy Selector Change
    document.getElementById('strategy-select').addEventListener('change', async (e) => {
        currentStrategy = e.target.value;
        await loadReport();
    });

    // Run What-If Simulation
    document.getElementById('run-whatif-btn').addEventListener('click', async () => {
        const checkedBoxes = document.querySelectorAll('#whatif-checkboxes input[type="checkbox"]:checked');
        const removeIds = Array.from(checkedBoxes).map(cb => cb.value);

        const resultCard = document.getElementById('whatif-result-card');
        resultCard.innerHTML = '<div style="text-align: center; color: var(--accent-blue);">Simulating counter-factual timeline with suppressed events...</div>';

        try {
            const whatif = await API.whatIf(currentRunId, currentStrategy, removeIds);
            
            const orig = whatif.original;
            const mod = whatif.modified;
            const delta = whatif.confidence_delta;
            const deltaSign = delta > 0 ? `+${delta}%` : `${delta}%`;
            const deltaColor = delta > 0 ? '#10b981' : (delta < 0 ? '#ef4444' : 'var(--text-muted)');

            resultCard.innerHTML = `
                <div style="font-size: 0.8rem; color: var(--text-muted); text-transform: uppercase; font-weight: 700; margin-bottom: 0.5rem;">
                    Simulation Outcome (${whatif.removed_event_ids.length} Events Suppressed)
                </div>
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 0.75rem; margin-bottom: 0.75rem;">
                    <div style="background: var(--bg-card); padding: 0.65rem; border-radius: var(--radius-sm); border: 1px solid var(--border-subtle);">
                        <div style="font-size: 0.75rem; color: var(--text-muted);">Baseline Root Cause</div>
                        <div style="font-weight: 700; color: #fca5a5;">${orig.root_cause_type} (#${orig.root_event_id})</div>
                        <div style="font-size: 0.8rem; color: var(--text-muted);">${orig.confidence}% Confidence</div>
                    </div>
                    <div style="background: var(--bg-card); padding: 0.65rem; border-radius: var(--radius-sm); border: 1px solid ${whatif.root_cause_changed ? '#10b981' : 'var(--border-subtle)'};">
                        <div style="font-size: 0.75rem; color: var(--text-muted);">Post-Removal Root Cause</div>
                        <div style="font-weight: 700; color: ${whatif.root_cause_changed ? '#10b981' : '#fca5a5'};">${mod.root_cause_type} (#${mod.root_event_id})</div>
                        <div style="font-size: 0.8rem; color: ${deltaColor}; font-weight: 600;">${mod.confidence}% (${deltaSign})</div>
                    </div>
                </div>
                <div style="font-size: 0.85rem; color: var(--text-primary); background: var(--bg-card); padding: 0.65rem; border-radius: var(--radius-sm); border-left: 3px solid #6366f1;">
                    ${escapeHtml(whatif.diff_summary)}
                </div>
            `;

        } catch (err) {
            resultCard.innerHTML = `<div style="color: var(--accent-red); font-size: 0.85rem;">Simulation failed: ${err.message}</div>`;
        }
    });

    // Export Buttons
    document.getElementById('export-txt-btn').addEventListener('click', () => {
        API.getReport(currentRunId, 'txt');
    });

    document.getElementById('export-json-btn').addEventListener('click', async () => {
        const jsonReport = await API.getReport(currentRunId, 'json');
        const blob = new Blob([JSON.stringify(jsonReport, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `tracex_report_run_${currentRunId}.json`;
        a.click();
        URL.revokeObjectURL(url);
    });
}

function escapeHtml(text) {
    if (!text) return '';
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}
