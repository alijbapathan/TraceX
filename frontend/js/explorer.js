/**
 * TraceX Log Explorer Controller
 */

let currentPage = 1;
let totalPages = 1;
let currentRunId = '';

document.addEventListener('DOMContentLoaded', async () => {
    setupEventListeners();

    await API.initRunSelector(async (runId) => {
        currentRunId = runId;
        currentPage = 1;
        await loadLogs();
    });
});

async function loadLogs() {
    if (!currentRunId) return;

    const tbody = document.getElementById('logs-tbody');
    tbody.innerHTML = '<tr><td colspan="6" style="text-align: center; color: var(--text-muted); padding: 1.5rem;">Loading events...</td></tr>';

    const level = document.getElementById('level-select').value;
    const source = document.getElementById('source-select').value;
    const q = document.getElementById('search-input').value.trim();
    const from = document.getElementById('from-input').value.trim();
    const to = document.getElementById('to-input').value.trim();
    const limit = parseInt(document.getElementById('limit-select').value);

    try {
        const data = await API.getLogs(currentRunId, { level, source, q, from, to, page: currentPage, limit });
        renderTable(data);
    } catch (err) {
        tbody.innerHTML = `<tr><td colspan="6" style="text-align: center; color: var(--accent-red); padding: 1.5rem;">Error: ${err.message}</td></tr>`;
    }
}

function renderTable(data) {
    const tbody = document.getElementById('logs-tbody');
    tbody.innerHTML = '';

    const events = data.events || [];
    totalPages = data.total_pages || 1;
    currentPage = data.page || 1;

    document.getElementById('log-count-summary').textContent = `Showing ${events.length} of ${data.total.toLocaleString()} matching events`;
    document.getElementById('page-info').textContent = `Page ${currentPage} of ${totalPages} (${data.total.toLocaleString()} total)`;

    document.getElementById('prev-page-btn').disabled = (currentPage <= 1);
    document.getElementById('next-page-btn').disabled = (currentPage >= totalPages);

    if (events.length === 0) {
        tbody.innerHTML = '<tr><td colspan="6" style="text-align: center; color: var(--text-muted); padding: 2rem;">No matching events found.</td></tr>';
        return;
    }

    events.forEach(ev => {
        const tr = document.createElement('tr');
        tr.style.cursor = 'pointer';

        let badgeClass = 'badge-info';
        if (ev.level === 'WARN') badgeClass = 'badge-warn';
        else if (ev.level === 'ERROR') badgeClass = 'badge-error';
        else if (ev.level === 'FATAL') badgeClass = 'badge-fatal';

        let typeBadge = '';
        if (ev.type && ev.type !== 'NORMAL') {
            typeBadge = `<span class="badge badge-chain" style="margin-left: 4px;">${ev.type}</span>`;
        } else {
            typeBadge = `<span style="color: var(--text-muted); font-size: 0.75rem;">NORMAL</span>`;
        }

        tr.innerHTML = `
            <td class="mono" style="color: var(--text-muted); font-weight: 600;">#${ev.id}</td>
            <td class="mono" style="font-size: 0.8rem;">${ev.ts}</td>
            <td><span class="badge ${badgeClass}">${ev.level}</span></td>
            <td><span class="mono" style="color: var(--accent-blue);">${ev.source}</span></td>
            <td>${typeBadge}</td>
            <td style="color: ${ev.level === 'ERROR' || ev.level === 'FATAL' ? '#fca5a5' : 'var(--text-primary)'};">${escapeHtml(ev.message)}</td>
        `;

        tr.addEventListener('click', () => showDetailModal(ev));
        tbody.appendChild(tr);
    });
}

function showDetailModal(ev) {
    const modal = document.getElementById('detail-modal');
    document.getElementById('modal-title').textContent = `Event #${ev.id} - ${ev.source} (${ev.level})`;
    document.getElementById('modal-body').textContent = JSON.stringify(ev, null, 2);
    modal.classList.add('active');
}

function escapeHtml(text) {
    if (!text) return '';
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function setupEventListeners() {
    document.getElementById('apply-filter-btn').addEventListener('click', () => {
        currentPage = 1;
        loadLogs();
    });

    document.getElementById('search-input').addEventListener('keypress', (e) => {
        if (e.key === 'Enter') {
            currentPage = 1;
            loadLogs();
        }
    });

    document.getElementById('level-select').addEventListener('change', () => {
        currentPage = 1;
        loadLogs();
    });

    document.getElementById('source-select').addEventListener('change', () => {
        currentPage = 1;
        loadLogs();
    });

    document.getElementById('limit-select').addEventListener('change', () => {
        currentPage = 1;
        loadLogs();
    });

    document.getElementById('prev-page-btn').addEventListener('click', () => {
        if (currentPage > 1) {
            currentPage--;
            loadLogs();
        }
    });

    document.getElementById('next-page-btn').addEventListener('click', () => {
        if (currentPage < totalPages) {
            currentPage++;
            loadLogs();
        }
    });

    document.getElementById('reset-filters-btn').addEventListener('click', () => {
        document.getElementById('search-input').value = '';
        document.getElementById('level-select').value = '';
        document.getElementById('source-select').value = '';
        document.getElementById('from-input').value = '';
        document.getElementById('to-input').value = '';
        currentPage = 1;
        loadLogs();
    });

    document.getElementById('close-detail-modal').addEventListener('click', () => {
        document.getElementById('detail-modal').classList.remove('active');
    });
}
