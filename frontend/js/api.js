/**
 * TraceX API Client
 * Shared REST communication library connecting frontend to C++ backend.
 */

const API = {
    baseUrl: window.location.origin.includes('http') ? window.location.origin : 'http://localhost:8080',

    async getRuns() {
        const resp = await fetch(`${this.baseUrl}/api/runs`);
        if (!resp.ok) throw new Error(await resp.text());
        return await resp.json();
    },

    async getStats(runId) {
        const url = runId ? `${this.baseUrl}/api/stats?run_id=${runId}` : `${this.baseUrl}/api/stats`;
        const resp = await fetch(url);
        if (!resp.ok) throw new Error(await resp.text());
        return await resp.json();
    },

    async getLogs(runId, { level = '', source = '', q = '', from = '', to = '', page = 1, limit = 50 } = {}) {
        const params = new URLSearchParams({
            run_id: runId || 0,
            level, source, q, from, to, page, limit
        });
        const resp = await fetch(`${this.baseUrl}/api/logs?${params.toString()}`);
        if (!resp.ok) throw new Error(await resp.text());
        return await resp.json();
    },

    async getTimeline(runId) {
        const url = runId ? `${this.baseUrl}/api/timeline?run_id=${runId}` : `${this.baseUrl}/api/timeline`;
        const resp = await fetch(url);
        if (!resp.ok) throw new Error(await resp.text());
        return await resp.json();
    },

    async getGraph(runId, strategy = 'rule') {
        const url = `${this.baseUrl}/api/graph?run_id=${runId || 0}&strategy=${strategy}`;
        const resp = await fetch(url);
        if (!resp.ok) throw new Error(await resp.text());
        return await resp.json();
    },

    async analyze(runId, strategy = 'rule') {
        const resp = await fetch(`${this.baseUrl}/api/analyze`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ run_id: parseInt(runId), strategy })
        });
        if (!resp.ok) throw new Error(await resp.text());
        return await resp.json();
    },

    async whatIf(runId, strategy = 'rule', removeEventIds = []) {
        const resp = await fetch(`${this.baseUrl}/api/whatif`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                run_id: parseInt(runId),
                strategy,
                remove_event_ids: removeEventIds.map(x => parseInt(x))
            })
        });
        if (!resp.ok) throw new Error(await resp.text());
        return await resp.json();
    },

    async compare(runId) {
        const url = `${this.baseUrl}/api/compare?run_id=${runId || 0}`;
        const resp = await fetch(url);
        if (!resp.ok) throw new Error(await resp.text());
        return await resp.json();
    },

    async getReport(runId, format = 'json') {
        const url = `${this.baseUrl}/api/report?run_id=${runId || 0}&format=${format}`;
        if (format === 'txt') {
            window.location.href = url;
            return;
        }
        const resp = await fetch(url);
        if (!resp.ok) throw new Error(await resp.text());
        return await resp.json();
    },

    async uploadLog(file) {
        const formData = new FormData();
        formData.append('file', file);
        const resp = await fetch(`${this.baseUrl}/api/upload`, {
            method: 'POST',
            body: formData
        });
        if (!resp.ok) throw new Error(await resp.text());
        return await resp.json();
    },

    // Current Active Run ID helper
    getCurrentRunId() {
        return localStorage.getItem('tracex_active_run') || '';
    },

    setCurrentRunId(id) {
        localStorage.setItem('tracex_active_run', id);
    },

    // Navbar Run Selector initialization
    async initRunSelector(onChangeCallback) {
        const selector = document.getElementById('run-selector');
        if (!selector) return;

        try {
            const runs = await this.getRuns();
            selector.innerHTML = '';
            
            if (runs.length === 0) {
                const opt = document.createElement('option');
                opt.value = '';
                opt.textContent = 'No runs available (Upload a log)';
                selector.appendChild(opt);
                return;
            }

            let activeId = this.getCurrentRunId();
            let matched = false;

            runs.forEach(r => {
                const opt = document.createElement('option');
                opt.value = r.id;
                opt.textContent = `Run #${r.id}: ${r.file_name} (${r.total_events} evts)`;
                if (String(r.id) === String(activeId)) {
                    opt.selected = true;
                    matched = true;
                }
                selector.appendChild(opt);
            });

            if (!matched && runs.length > 0) {
                activeId = runs[0].id;
                this.setCurrentRunId(activeId);
            }

            selector.addEventListener('change', (e) => {
                this.setCurrentRunId(e.target.value);
                if (onChangeCallback) onChangeCallback(e.target.value);
            });

            if (onChangeCallback) onChangeCallback(activeId);

        } catch (err) {
            console.error('Failed to load runs:', err);
        }
    }
};

window.API = API;
