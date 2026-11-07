/**
 * TraceX Dashboard Controller
 */

let timelineChart = null;
let sourceChart = null;
let liveInterval = null;
let isLive = false;

document.addEventListener('DOMContentLoaded', async () => {
    initCharts();
    setupEventListeners();

    await API.initRunSelector(async (runId) => {
        await loadDashboardData(runId);
    });
});

function initCharts() {
    // 1. Errors Per Minute Line Chart
    const ctxTimeline = document.getElementById('timelineChart').getContext('2d');
    timelineChart = new Chart(ctxTimeline, {
        type: 'line',
        data: {
            labels: [],
            datasets: [{
                label: 'Errors & Fatalities',
                data: [],
                borderColor: '#ef4444',
                backgroundColor: 'rgba(239, 68, 68, 0.15)',
                borderWidth: 2,
                fill: true,
                tension: 0.3,
                pointBackgroundColor: '#ef4444',
                pointRadius: 4
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: { display: false },
                tooltip: {
                    callbacks: {
                        label: (ctx) => ` Errors: ${ctx.parsed.y}`
                    }
                }
            },
            scales: {
                x: {
                    grid: { color: '#1e293b' },
                    ticks: { color: '#94a3b8', font: { size: 11 } }
                },
                y: {
                    beginAtZero: true,
                    grid: { color: '#1e293b' },
                    ticks: { color: '#94a3b8', precision: 0 }
                }
            }
        }
    });

    // 2. Errors by Source Doughnut Chart
    const ctxSource = document.getElementById('sourceChart').getContext('2d');
    sourceChart = new Chart(ctxSource, {
        type: 'doughnut',
        data: {
            labels: [],
            datasets: [{
                data: [],
                backgroundColor: ['#ef4444', '#f59e0b', '#3b82f6', '#8b5cf6', '#10b981', '#ec4899', '#64748b'],
                borderWidth: 1,
                borderColor: '#111827'
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: {
                    position: 'right',
                    labels: { color: '#94a3b8', font: { size: 11 } }
                }
            }
        }
    });
}

async function loadDashboardData(runId) {
    if (!runId) return;

    try {
        const stats = await API.getStats(runId);
        const runs = await API.getRuns();
        const activeRun = runs.find(r => String(r.id) === String(runId));

        // Update cards
        document.getElementById('card-total').textContent = stats.total_events.toLocaleString();
        document.getElementById('card-total-sub').textContent = activeRun ? `File: ${activeRun.file_name}` : 'Ready';

        document.getElementById('card-errors').textContent = stats.error_count.toLocaleString();
        document.getElementById('card-errors-sub').textContent = `${stats.fatal_count} fatal, ${stats.error_count - stats.fatal_count} errors`;

        document.getElementById('card-warnings').textContent = stats.warn_count.toLocaleString();

        if (activeRun && activeRun.root_cause && activeRun.root_cause !== 'PENDING') {
            document.getElementById('card-root').textContent = activeRun.root_cause;
            document.getElementById('card-confidence-badge').textContent = `${Math.round(activeRun.confidence)}% CONF`;
            document.getElementById('card-root-sub').textContent = `Execution: ${activeRun.exec_ms.toFixed(2)} ms`;
        } else {
            document.getElementById('card-root').textContent = 'NOMINAL';
            document.getElementById('card-confidence-badge').textContent = '100% HEALTHY';
            document.getElementById('card-root-sub').textContent = 'No failure cascade detected';
        }

        // Update Timeline Chart
        const epm = stats.errors_per_minute || [];
        timelineChart.data.labels = epm.map(e => e.time.length > 11 ? e.time.substr(11) : e.time);
        timelineChart.data.datasets[0].data = epm.map(e => e.count);
        timelineChart.update();

        // Update Source Chart
        const errSource = stats.errors_by_source || {};
        const sources = Object.keys(errSource);
        const counts = Object.values(errSource);
        sourceChart.data.labels = sources.length ? sources : ['No Errors'];
        sourceChart.data.datasets[0].data = counts.length ? counts : [1];
        sourceChart.data.datasets[0].backgroundColor = sources.length ?
            ['#ef4444', '#f59e0b', '#3b82f6', '#8b5cf6', '#10b981', '#ec4899', '#64748b'] : ['#1e293b'];
        sourceChart.update();

    } catch (err) {
        console.error('Error fetching dashboard stats:', err);
    }
}

function setupEventListeners() {
    // Refresh button
    document.getElementById('refresh-btn').addEventListener('click', () => {
        loadDashboardData(API.getCurrentRunId());
    });

    // Live Mode Toggle
    const liveBtn = document.getElementById('live-toggle-btn');
    const liveIndicator = document.getElementById('live-indicator');
    liveBtn.addEventListener('click', () => {
        isLive = !isLive;
        if (isLive) {
            liveBtn.classList.remove('btn-outline');
            liveBtn.classList.add('btn-primary');
            liveBtn.innerHTML = '<span style="display:inline-block; width:8px; height:8px; border-radius:50%; background:#10b981; margin-right:4px; box-shadow:0 0 6px #10b981;"></span> Live Mode: ON (2s)';
            liveInterval = setInterval(() => {
                loadDashboardData(API.getCurrentRunId());
            }, 2000);
        } else {
            clearInterval(liveInterval);
            liveBtn.classList.remove('btn-primary');
            liveBtn.classList.add('btn-outline');
            liveBtn.innerHTML = '<span style="display:inline-block; width:8px; height:8px; border-radius:50%; background:#64748b; margin-right:4px;"></span> Live Mode: OFF';
        }
    });

    // Modal controls
    const uploadModal = document.getElementById('upload-modal');
    const uploadBtn = document.getElementById('upload-btn');
    const closeModalBtn = document.getElementById('close-modal-btn');
    const dropZone = document.getElementById('drop-zone');
    const fileInput = document.getElementById('file-input');
    const statusDiv = document.getElementById('upload-status');

    uploadBtn.addEventListener('click', () => {
        uploadModal.classList.add('active');
        statusDiv.style.display = 'none';
    });

    closeModalBtn.addEventListener('click', () => {
        uploadModal.classList.remove('active');
    });

    dropZone.addEventListener('click', () => fileInput.click());

    dropZone.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropZone.style.borderColor = 'var(--accent-blue)';
    });

    dropZone.addEventListener('dragleave', () => {
        dropZone.style.borderColor = 'var(--border-color)';
    });

    dropZone.addEventListener('drop', (e) => {
        e.preventDefault();
        dropZone.style.borderColor = 'var(--border-color)';
        if (e.dataTransfer.files.length) {
            handleFileUpload(e.dataTransfer.files[0]);
        }
    });

    fileInput.addEventListener('change', (e) => {
        if (e.target.files.length) {
            handleFileUpload(e.target.files[0]);
        }
    });

    async function handleFileUpload(file) {
        statusDiv.style.display = 'block';
        statusDiv.style.color = 'var(--accent-blue)';
        statusDiv.textContent = `Uploading and analyzing ${file.name}...`;

        try {
            const res = await API.uploadLog(file);
            statusDiv.style.color = 'var(--accent-green)';
            statusDiv.textContent = `Success! Parsed ${res.total_events} events. Possible root cause: ${res.root_cause} (${res.confidence}%)`;

            setTimeout(async () => {
                uploadModal.classList.remove('active');
                API.setCurrentRunId(res.run_id);
                await API.initRunSelector(loadDashboardData);
            }, 1000);

        } catch (err) {
            statusDiv.style.color = 'var(--accent-red)';
            statusDiv.textContent = `Upload failed: ${err.message}`;
        }
    }

    // Demo Scenario Buttons
    document.querySelectorAll('.demo-btn').forEach(btn => {
        btn.addEventListener('click', async () => {
            const path = btn.getAttribute('data-file');
            btn.disabled = true;
            const originalText = btn.textContent;
            btn.textContent = 'Loading...';

            try {
                // Fetch the sample file content and upload it
                const resp = await fetch(path);
                if (!resp.ok) throw new Error(`Could not fetch demo file ${path}`);
                const blob = await resp.blob();
                const file = new File([blob], path.split('/').pop(), { type: 'text/plain' });
                const res = await API.uploadLog(file);

                API.setCurrentRunId(res.run_id);
                await API.initRunSelector(loadDashboardData);
            } catch (err) {
                alert(`Error loading demo scenario: ${err.message}`);
            } finally {
                btn.disabled = false;
                btn.textContent = originalText;
            }
        });
    });
}
