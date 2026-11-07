/**
 * TraceX Dependency Graph Controller
 */

let network = null;
let currentNodes = [];
let currentRunId = '';

document.addEventListener('DOMContentLoaded', async () => {
    setupEventListeners();

    await API.initRunSelector(async (runId) => {
        currentRunId = runId;
        await loadGraph();
    });
});

async function loadGraph() {
    if (!currentRunId) return;

    const container = document.getElementById('network-graph');
    const strategy = document.getElementById('graph-strategy-select').value;

    try {
        const [graphData, logsData] = await Promise.all([
            API.getGraph(currentRunId, strategy),
            API.getLogs(currentRunId, { limit: 1000 })
        ]);

        const rawEvents = logsData.events || [];
        const eventMap = new Map(rawEvents.map(e => [e.id, e]));

        currentNodes = graphData.nodes || [];
        const edgesData = graphData.edges || [];

        if (currentNodes.length === 0) {
            container.innerHTML = '<div style="display:flex; height:100%; align-items:center; justify-content:center; color:var(--text-muted);">No failure cascades or correlated dependencies identified for this run.</div>';
            clearInspector();
            return;
        }

        const visNodes = currentNodes.map(n => {
            const ev = eventMap.get(n.id) || {};
            const isRoot = n.isRoot;
            return {
                id: n.id,
                label: n.label,
                title: `${n.source}: ${ev.message || n.type}`,
                color: {
                    background: isRoot ? '#ef4444' : '#f59e0b',
                    border: isRoot ? '#ffffff' : '#d97706',
                    highlight: {
                        background: isRoot ? '#dc2626' : '#b45309',
                        border: '#ffffff'
                    }
                },
                font: { color: '#ffffff', face: 'Inter', size: isRoot ? 14 : 12, bold: isRoot },
                shape: isRoot ? 'box' : 'dot',
                size: isRoot ? 25 : 18,
                borderWidth: isRoot ? 3 : 2,
                shadow: isRoot ? { enabled: true, color: 'rgba(239, 68, 68, 0.7)', size: 15 } : false
            };
        });

        const visEdges = edgesData.map((e, idx) => ({
            id: idx,
            from: e.from,
            to: e.to,
            arrows: 'to',
            label: e.label,
            font: { color: '#94a3b8', size: 10, align: 'horizontal' },
            color: { color: '#ef4444', highlight: '#f87171' },
            width: 2,
            smooth: { type: 'curvedCW', roundness: 0.2 }
        }));

        const data = {
            nodes: new vis.DataSet(visNodes),
            edges: new vis.DataSet(visEdges)
        };

        const options = {
            physics: {
                solver: 'forceAtlas2Based',
                forceAtlas2Based: {
                    gravitationalConstant: -50,
                    centralGravity: 0.01,
                    springLength: 120,
                    springConstant: 0.08
                },
                maxVelocity: 50,
                minVelocity: 0.1,
                stabilization: { iterations: 150 }
            },
            interaction: {
                hover: true,
                navigationButtons: false,
                keyboard: true
            }
        };

        network = new vis.Network(container, data, options);

        network.on('click', (params) => {
            if (params.nodes.length > 0) {
                const nodeId = params.nodes[0];
                const nodeInfo = currentNodes.find(n => n.id === nodeId);
                const ev = eventMap.get(nodeId);
                showNodeDetails(nodeInfo, ev);
            } else {
                clearInspector();
            }
        });

        // Automatically inspect the root node
        const rootNode = currentNodes.find(n => n.isRoot);
        if (rootNode) {
            showNodeDetails(rootNode, eventMap.get(rootNode.id));
        }

    } catch (err) {
        container.innerHTML = `<div style="display:flex; height:100%; align-items:center; justify-content:center; color:var(--accent-red);">Error: ${err.message}</div>`;
    }
}

function showNodeDetails(node, event) {
    if (!node) return;
    document.getElementById('inspector-empty').style.display = 'none';
    document.getElementById('inspector-content').style.display = 'block';

    const isRoot = node.isRoot;
    const badge = document.getElementById('inspector-badge');
    if (isRoot) {
        badge.className = 'badge badge-root';
        badge.textContent = 'Possible Root Cause';
        document.getElementById('root-cause-banner').style.display = 'block';
    } else {
        badge.className = 'badge badge-chain';
        badge.textContent = 'Cascaded Failure';
        document.getElementById('root-cause-banner').style.display = 'none';
    }

    document.getElementById('node-name').textContent = `Event #${node.id} (${node.source})`;
    document.getElementById('node-ts').textContent = node.timestamp || (event ? event.ts : '--');
    document.getElementById('node-level').textContent = node.level || (event ? event.level : '--');
    document.getElementById('node-level').style.color = (node.level === 'ERROR' || node.level === 'FATAL') ? '#ef4444' : '#38bdf8';
    document.getElementById('node-type').textContent = node.type || '--';
    document.getElementById('node-msg').textContent = event ? event.message : 'No message recorded';
}

function clearInspector() {
    document.getElementById('inspector-empty').style.display = 'block';
    document.getElementById('inspector-content').style.display = 'none';
    const badge = document.getElementById('inspector-badge');
    badge.className = 'badge badge-info';
    badge.textContent = 'Select a node';
}

function setupEventListeners() {
    document.getElementById('graph-strategy-select').addEventListener('change', () => {
        loadGraph();
    });

    document.getElementById('reset-view-btn').addEventListener('click', () => {
        if (network) network.fit({ animation: { duration: 600, easingFunction: 'easeInOutQuad' } });
    });
}
