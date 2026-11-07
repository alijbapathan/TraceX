/**
 * TraceX Failure Timeline Controller
 */

let allTimelineEvents = [];
let rootEventId = 0;
let currentRunId = '';

document.addEventListener('DOMContentLoaded', async () => {
    setupEventListeners();

    await API.initRunSelector(async (runId) => {
        currentRunId = runId;
        await loadTimeline();
    });
});

async function loadTimeline() {
    if (!currentRunId) return;

    const container = document.getElementById('timeline-list');
    container.innerHTML = '<div style="padding: 2rem; text-align: center; color: var(--text-muted);">Reconstructing timeline sequence...</div>';

    try {
        const [timelineData, runs] = await Promise.all([
            API.getTimeline(currentRunId),
            API.getRuns()
        ]);

        const activeRun = runs.find(r => String(r.id) === String(currentRunId));
        // Retrieve root event from graph or analyze endpoint
        try {
            const graphData = await API.getGraph(currentRunId);
            rootEventId = graphData.root_event_id || 0;
        } catch {
            rootEventId = 0;
        }

        allTimelineEvents = timelineData || [];
        renderTimeline();

    } catch (err) {
        container.innerHTML = `<div style="padding: 2rem; text-align: center; color: var(--accent-red);">Error: ${err.message}</div>`;
    }
}

function renderTimeline() {
    const container = document.getElementById('timeline-list');
    container.innerHTML = '';

    const errorsOnly = document.getElementById('filter-errors-only').checked;
    const eventsToRender = errorsOnly
        ? allTimelineEvents.filter(e => e.level === 'ERROR' || e.level === 'FATAL' || e.in_chain || e.id === rootEventId)
        : allTimelineEvents;

    document.getElementById('timeline-count').textContent = `Showing ${eventsToRender.length} of ${allTimelineEvents.length} events`;

    if (eventsToRender.length === 0) {
        container.innerHTML = '<div style="padding: 2rem; text-align: center; color: var(--text-muted);">No critical or chain events found in this run.</div>';
        return;
    }

    eventsToRender.forEach((ev, idx) => {
        const item = document.createElement('div');
        item.className = 'timeline-item';

        const isRoot = (ev.id === rootEventId);
        const isError = (ev.level === 'ERROR' || ev.level === 'FATAL');
        const inChain = ev.in_chain;

        let dotClass = 'timeline-dot';
        if (isRoot) dotClass += ' root';
        else if (inChain) dotClass += ' chain';
        else if (isError) dotClass += ' error';

        let badgeClass = 'badge-info';
        if (ev.level === 'WARN') badgeClass = 'badge-warn';
        else if (ev.level === 'ERROR') badgeClass = 'badge-error';
        else if (ev.level === 'FATAL') badgeClass = 'badge-fatal';

        let chainTags = '';
        if (isRoot) {
            chainTags += `<span class="badge badge-root" style="margin-left: 6px;">POSSIBLE ROOT CAUSE</span>`;
        }
        if (inChain) {
            chainTags += `<span class="badge badge-chain" style="margin-left: 6px;">IN FAILURE CHAIN</span>`;
        }

        item.innerHTML = `
            <div class="${dotClass}"></div>
            <div style="background: ${isRoot ? 'rgba(239, 68, 68, 0.12)' : (inChain ? 'rgba(99, 102, 241, 0.08)' : 'var(--bg-dark)')};
                        border: 1px solid ${isRoot ? '#ef4444' : (inChain ? '#6366f1' : 'var(--border-subtle)')};
                        border-radius: var(--radius-sm); padding: 0.75rem 1rem; cursor: pointer; transition: transform 0.15s;">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 0.35rem;">
                    <div style="display: flex; align-items: center; gap: 0.5rem;">
                        <span class="mono" style="font-weight: 700; color: var(--text-muted);">#${ev.id}</span>
                        <span class="badge ${badgeClass}">${ev.level}</span>
                        <span class="mono" style="color: var(--accent-blue); font-weight: 600;">${ev.source}</span>
                        ${chainTags}
                    </div>
                    <span class="mono" style="font-size: 0.75rem; color: var(--text-muted);">${ev.ts}</span>
                </div>
                <div style="color: ${isError ? '#fca5a5' : 'var(--text-primary)'}; font-size: 0.85rem;">
                    ${escapeHtml(ev.message)}
                </div>
            </div>
        `;

        item.querySelector('div[style*="cursor: pointer"]').addEventListener('click', () => {
            showDetailModal(ev);
        });

        container.appendChild(item);
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
    document.getElementById('filter-errors-only').addEventListener('change', () => {
        renderTimeline();
    });

    document.getElementById('close-modal-btn').addEventListener('click', () => {
        document.getElementById('detail-modal').classList.remove('active');
    });
}
