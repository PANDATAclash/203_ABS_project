/**
 * @license
 * SPDX-License-Identifier: Apache-2.0
 */

import React, { useState, useMemo, useRef, useEffect } from 'react';
import Papa from 'papaparse';
import {
  LineChart, Line, XAxis, YAxis, CartesianGrid,
  Tooltip, Legend, ResponsiveContainer, ReferenceArea,
} from 'recharts';
import {
  FileUp, Download, Activity, Table as TableIcon,
  LayoutDashboard, Filter, RefreshCw, Info, Hand, ZoomIn,
} from 'lucide-react';
import { useDropzone } from 'react-dropzone';
import { toPng } from 'html-to-image';
import { clsx, type ClassValue } from 'clsx';
import { twMerge } from 'tailwind-merge';
import logoUrl from './politielogo.png';

function cn(...inputs: ClassValue[]) {
  return twMerge(clsx(inputs));
}

// --- Types ---
interface DataPoint { [key: string]: number | string; }
interface Metadata  { fileName: string; rowCount: number; columns: string[]; }
interface AxisConfig {
  id: string; label: string;
  orientation: 'left' | 'right'; offset: number;
}

// --- Constants ---
const SIGNAL_COLORS = [
  '#2563eb','#dc2626','#16a34a','#d97706','#7c3aed',
  '#db2777','#0891b2','#4f46e5','#ea580c','#65a30d',
];

// Each Y-axis lane width in px — keep tight so axes stay close together
const AXIS_W = 42;

const UNIT_DISPLAY: Record<string, string> = {
  mps2:'m/s²', ms2:'m/s²', mps:'m/s',
  kph:'km/h',  kmh:'km/h', ms:'m/s',
  bar:'bar',   deg:'°',    rad:'rad',
  rpm:'rpm',   n:'N',      nm:'N·m',
  pct:'%',     value:'',
};

function extractUnit(col: string): string {
  const b = col.match(/\[(.*?)\]/);   if (b?.[1]) return b[1].trim().toLowerCase();
  const p = col.match(/\((.*?)\)/);   if (p?.[1]) return p[1].trim().toLowerCase();
  const s = col.match(/[-_]([a-zA-Z°/%²³]+\d*)$/); if (s?.[1]) return s[1].trim().toLowerCase();
  return 'value';
}
function prettyUnit(raw: string): string {
  return UNIT_DISPLAY[raw.toLowerCase()] ?? raw;
}

// ─── App ────────────────────────────────────────────────────────────────────

export default function App() {
  const [data,            setData]            = useState<DataPoint[]>([]);
  const [metadata,        setMetadata]        = useState<Metadata | null>(null);
  const [selectedSignals, setSelectedSignals] = useState<string[]>([]);
  const [timeColumn,      setTimeColumn]      = useState<string>('');
  const [logoError,       setLogoError]       = useState(false);
  const [activeTab,       setActiveTab]       = useState<'plot' | 'table'>('plot');
  const [containerSize,   setContainerSize]   = useState({ width: 0, height: 0 });

  // Export wrapper ref (whole chart card)
  const chartRef          = useRef<HTMLDivElement>(null);
  // Inner chart container ref (where we attach mouse events)
  const containerRef      = useRef<HTMLDivElement>(null);

  // ── Domain: state for recharts + mirrored refs for handlers ──
  const [left,  setLeft]  = useState<number|'dataMin'>('dataMin');
  const [right, setRight] = useState<number|'dataMax'>('dataMax');
  const leftRef  = useRef<number|'dataMin'>('dataMin');
  const rightRef = useRef<number|'dataMax'>('dataMax');

  function applyDomain(l: number|'dataMin', r: number|'dataMax') {
    leftRef.current = l; rightRef.current = r; setLeft(l); setRight(r);
  }

  // Zoom selection box
  const [zoomBox,    setZoomBox]    = useState<{x1:number;x2:number}|null>(null);
  const zoomBoxRef = useRef<{x1:number;x2:number}|null>(null);

  // Cursor state
  const [cursor, setCursor] = useState<string>('grab');

  // Drag state — only a ref, no re-renders mid-drag
  const drag = useRef<{
    button:number; startX:number; dl:number; dr:number;
  }|null>(null);

  // Live data refs so handlers never go stale
  const dataRef       = useRef<DataPoint[]>([]);
  const tcRef         = useRef<string>('');        // time-column
  const marginsRef    = useRef({top:10,bottom:10,left:AXIS_W,right:AXIS_W});

  useEffect(()=>{ dataRef.current = data; },[data]);
  useEffect(()=>{ tcRef.current   = timeColumn; },[timeColumn]);

  // ── CSV load ──
  const onDrop = (files: File[]) => {
    const file = files[0]; if (!file) return;
    Papa.parse(file,{
      header:true, dynamicTyping:true, skipEmptyLines:true,
      complete:(res)=>{
        const cols   = res.meta.fields || [];
        let parsed = res.data as DataPoint[];
        
        const tc = cols.find(c=>
          c.toLowerCase().includes('time')||
          c.toLowerCase().includes('timestamp')||
          c.toLowerCase()==='t'
        )||cols[0];

        // Ensure time column is numeric for chart compatibility
        parsed = parsed.map(row => {
          const val = row[tc];
          if (typeof val === 'string') {
            const num = parseFloat(val);
            if (!isNaN(num)) return { ...row, [tc]: num };
          }
          return row;
        }).filter(row => typeof row[tc] === 'number' && !isNaN(row[tc] as number));

        setData(parsed);
        setMetadata({fileName:file.name, rowCount:parsed.length, columns:cols});
        setTimeColumn(tc);
        setSelectedSignals(cols.filter(c=>c!==tc).slice(0,3));
        applyDomain('dataMin','dataMax');
      },
      error:(e)=>{ console.error(e); alert('Failed to parse CSV.'); },
    });
  };

  const {getRootProps,getInputProps,isDragActive} = useDropzone({
    onDrop, accept:{'text/csv':['.csv']}, multiple:false,
  } as any);

  const toggleSignal = (sig:string) =>
    setSelectedSignals(p=> p.includes(sig)?p.filter(s=>s!==sig):[...p,sig]);

  // ── Export ──
  const handleExport = async () => {
    if(!chartRef.current) return;
    try {
      const url = await toPng(chartRef.current,{backgroundColor:'#fff',quality:1});
      const a = document.createElement('a');
      a.download=`forensic-plot-${Date.now()}.png`; a.href=url; a.click();
    } catch(e){ console.error(e); }
  };

  // ── Axis config ──
  const axisConfigs = useMemo<AxisConfig[]>(()=>{
    const units:string[] = [];
    selectedSignals.forEach(s=>{ const u=extractUnit(s); if(!units.includes(u)) units.push(u); });
    return units.map((u,i)=>({
      id:u, label:prettyUnit(u),
      orientation:(i%2===0?'left':'right') as 'left'|'right',
      offset: Math.floor(i/2)*AXIS_W,
    }));
  },[selectedSignals]);

  const sigUnit = useMemo(()=>
    selectedSignals.reduce<Record<string,string>>((a,s)=>{a[s]=extractUnit(s);return a;},{})
  ,[selectedSignals]);

  // Fixed margins to keep plot width constant regardless of axis count
  // Reduced from 140 to 110 to give more space to the plot while still allowing ~2-3 axes
  const margins = useMemo(() => ({ top: 10, bottom: 20, left: 110, right: 110 }), []);

  useEffect(() => {
    marginsRef.current = margins;
  }, [margins]);

  // ── Pixel → data value (reads only refs) ──
  function px2data(clientX:number):number {
    const el = containerRef.current;
    if(!el || dataRef.current.length===0) return 0;
    const rect = el.getBoundingClientRect();
    const pw   = rect.width - marginsRef.current.left - marginsRef.current.right;
    if(pw<=0) return 0;
    const ratio = Math.max(0,Math.min(1,(clientX-rect.left-marginsRef.current.left)/pw));
    const d=dataRef.current, tc=tcRef.current;
    const dl = leftRef.current ==='dataMin' ? (Number(d[0]?.[tc]) || 0) : Number(leftRef.current);
    const dr = rightRef.current==='dataMax' ? (Number(d[d.length-1]?.[tc]) || 1) : Number(rightRef.current);
    return dl + ratio*(dr-dl);
  }

  // ── Window mouse handlers — stored in refs so they're always current ──
  const onMoveRef = useRef<(e:MouseEvent)=>void>(()=>{});
  const onUpRef   = useRef<(e:MouseEvent)=>void>(()=>{});

  // Stable wrappers added to window ONCE; internally call the live refs
  const stableMove = useRef((e: MouseEvent) => onMoveRef.current(e));
  const stableUp = useRef((e: MouseEvent) => onUpRef.current(e));

  // Update live handlers every render (no stale closures) - moved into useEffect to avoid side effects in render
  useEffect(() => {
    onMoveRef.current = (e: MouseEvent) => {
      if (!drag.current || dataRef.current.length === 0) return;
      const { button, startX, dl, dr } = drag.current;

      if (button === 2) {
        // Zoom: stretch orange box
        const x1 = px2data(startX), x2 = px2data(e.clientX);
        if (!zoomBoxRef.current || zoomBoxRef.current.x1 !== x1 || zoomBoxRef.current.x2 !== x2) {
          zoomBoxRef.current = { x1, x2 };
          setZoomBox({ x1, x2 });
        }
      } else {
        // Pan: shift domain
        const el = containerRef.current; if (!el) return;
        const pw = el.getBoundingClientRect().width - marginsRef.current.left - marginsRef.current.right;
        if (pw <= 0) return;
        const span = dr - dl;
        const delta = ((e.clientX - startX) / pw) * span;
        const d = dataRef.current, tc = tcRef.current;
        const fMin = Number(d[0][tc]), fMax = Number(d[d.length - 1][tc]);
        let nl = dl - delta, nr = dr - delta;
        if (nl < fMin) { nl = fMin; nr = fMin + span; }
        if (nr > fMax) { nr = fMax; nl = fMax - span; }

        if (leftRef.current !== nl || rightRef.current !== nr) {
          leftRef.current = nl; rightRef.current = nr;
          setLeft(nl); setRight(nr);
        }
      }
    };

    onUpRef.current = (e: MouseEvent) => {
      if (drag.current?.button === 2 && zoomBoxRef.current) {
        let [l, r] = [zoomBoxRef.current.x1, zoomBoxRef.current.x2];
        if (l > r) [l, r] = [r, l];
        if (Math.abs(l - r) > 1e-10) {
          if (leftRef.current !== l || rightRef.current !== r) {
            leftRef.current = l; rightRef.current = r;
            setLeft(l); setRight(r);
          }
        }
        zoomBoxRef.current = null; setZoomBox(null);
      }
      drag.current = null; setCursor('grab');
      window.removeEventListener('mousemove', stableMove.current);
      window.removeEventListener('mouseup', stableUp.current);
    };
  });

  // ── Native mousedown in CAPTURE phase — fires BEFORE recharts consumes it ──
  // We re-attach whenever data changes so dataRef is guaranteed populated.
  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;

    const obs = new ResizeObserver(entries => {
      for (let entry of entries) {
        setContainerSize({ width: entry.contentRect.width, height: entry.contentRect.height });
      }
    });
    obs.observe(el);

    const onDown = (e: MouseEvent) => {
      if (dataRef.current.length === 0) return;
      e.preventDefault();
      const d = dataRef.current, tc = tcRef.current;
      const dl = leftRef.current === 'dataMin' ? (Number(d[0]?.[tc]) || 0) : Number(leftRef.current);
      const dr = rightRef.current === 'dataMax' ? (Number(d[d.length - 1]?.[tc]) || 1) : Number(rightRef.current);
      drag.current = { button: e.button, startX: e.clientX, dl, dr };
      if (e.button === 2) {
        const v = px2data(e.clientX);
        zoomBoxRef.current = { x1: v, x2: v }; setZoomBox({ x1: v, x2: v });
        setCursor('crosshair');
      } else {
        setCursor('grabbing');
      }
      window.addEventListener('mousemove', stableMove.current);
      window.addEventListener('mouseup', stableUp.current);
    };

    const onCtx = (e: Event) => e.preventDefault();

    // capture:true → fires before recharts' own handlers
    el.addEventListener('mousedown', onDown, true);
    el.addEventListener('contextmenu', onCtx, true);
    return () => {
      obs.disconnect();
      el.removeEventListener('mousedown', onDown, true);
      el.removeEventListener('contextmenu', onCtx, true);
    };
    // Re-attach after CSV load or tab switch so the closure has the right el reference
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [data, activeTab]);

  // Cleanup window listeners on unmount
  useEffect(()=>()=>{
    window.removeEventListener('mousemove',stableMove.current);
    window.removeEventListener('mouseup',  stableUp.current);
  },[]);

  const resetView = ()=>{
    applyDomain('dataMin','dataMax');
    setZoomBox(null); zoomBoxRef.current=null; drag.current=null; setCursor('grab');
  };

  // ─── Render ────────────────────────────────────────────────────────────────

  return (
    <div className="flex flex-col h-screen bg-[#f3f6f9] font-sans text-slate-900 overflow-hidden">

      {/* Header */}
      <header className="bg-gradient-to-r from-[#12315e] via-[#1c2b4a] to-[#11233f] text-white px-6 py-4 flex items-center justify-between shadow-lg z-10 border-b-4 border-[#ff6600]">
        <div className="flex items-center gap-4">
          <div className="w-14 h-14 bg-white rounded-lg flex items-center justify-center shadow-inner p-2 overflow-hidden">
            {!logoError
              ? <img src={logoUrl} alt="logo" className="w-full h-full object-contain" onError={()=>setLogoError(true)}/>
              : <Activity className="text-[#1c2b4a] w-7 h-7"/>}
          </div>
          <div>
            <h1 className="text-lg font-bold tracking-tight uppercase leading-tight">Politie Academie</h1>
            <p className="text-[10px] text-blue-100 uppercase tracking-[0.2em]">Forensisch Onderzoek Motorfiets Ongeluk</p>
          </div>
        </div>
        {metadata && (
          <div className="flex items-center gap-6 text-sm">
            <div className="flex flex-col items-end">
              <span className="text-blue-100 text-[10px] uppercase font-bold">Active File</span>
              <span className="font-mono text-xs">{metadata.fileName}</span>
            </div>
            <button onClick={handleExport}
              className="flex items-center gap-2 bg-[#ff6600] hover:bg-[#e65c00] transition-colors px-4 py-2 rounded font-bold text-xs uppercase">
              <Download size={16}/> Export PNG
            </button>
          </div>
        )}
      </header>

      <main className="flex flex-1 overflow-hidden">

        {/* Sidebar */}
        <aside className="w-80 bg-white border-r border-slate-200 flex flex-col shadow-sm">
          <div className="p-4 bg-[#f9fbfd]">
            <div {...getRootProps()} className={cn(
              'border-2 border-dashed rounded-xl p-6 transition-all cursor-pointer flex flex-col items-center justify-center text-center gap-2',
              isDragActive?'border-[#ff6600] bg-orange-50':'border-slate-200 hover:border-slate-300 bg-slate-50'
            )}>
              <input {...getInputProps()}/>
              <FileUp className={cn('w-8 h-8',isDragActive?'text-[#ff6600]':'text-slate-400')}/>
              <p className="text-xs font-semibold text-slate-600">{isDragActive?'Drop CSV here':'Load CSV Data'}</p>
              <p className="text-[10px] text-slate-400">SD Card Datalogger Format</p>
            </div>
          </div>

          <div className="flex-1 overflow-y-auto p-4 space-y-6">
            {metadata ? (
              <>
                <div>
                  <h3 className="text-[10px] font-bold text-slate-400 uppercase tracking-wider mb-3 flex items-center gap-2">
                    <Filter size={12}/> Signal Selection
                  </h3>
                  <div className="space-y-1">
                    {metadata.columns.map(col=>(
                      <label key={col} className={cn(
                        'flex items-center gap-3 p-2 rounded-lg cursor-pointer transition-all',
                        selectedSignals.includes(col)?'bg-slate-100':'hover:bg-slate-50'
                      )}>
                        <input type="checkbox"
                          checked={selectedSignals.includes(col)}
                          onChange={()=>toggleSignal(col)}
                          disabled={col===timeColumn}
                          className="w-4 h-4 rounded border-slate-300 text-[#1c2b4a] focus:ring-[#1c2b4a]"
                        />
                        <span className={cn('text-xs font-medium truncate flex-1',
                          selectedSignals.includes(col)?'text-slate-900':'text-slate-500')}>
                          {col}
                        </span>
                        {selectedSignals.includes(col)&&(
                          <div className="w-2 h-2 rounded-full flex-shrink-0"
                            style={{backgroundColor:SIGNAL_COLORS[selectedSignals.indexOf(col)%SIGNAL_COLORS.length]}}/>
                        )}
                      </label>
                    ))}
                  </div>
                </div>

                <div>
                  <h3 className="text-[10px] font-bold text-slate-400 uppercase tracking-wider mb-3 flex items-center gap-2">
                    <Info size={12}/> File Info
                  </h3>
                  <div className="bg-slate-50 rounded-lg p-3 space-y-2">
                    {[
                      ['Total Samples', metadata.rowCount.toLocaleString()],
                      ['Time Reference', timeColumn],
                      ['Active Signals', String(selectedSignals.length)],
                    ].map(([k,v])=>(
                      <div key={k} className="flex justify-between text-[10px]">
                        <span className="text-slate-500">{k}</span>
                        <span className="font-mono font-bold text-blue-600">{v}</span>
                      </div>
                    ))}
                  </div>
                </div>

                {/* Axis Map removed as per user request */}
              </>
            ):(
              <div className="h-full flex flex-col items-center justify-center text-center opacity-40 grayscale">
                <LayoutDashboard size={48} className="mb-4 text-slate-300"/>
                <p className="text-sm font-medium">No data loaded</p>
                <p className="text-[10px]">Import a CSV to begin analysis</p>
              </div>
            )}
          </div>
        </aside>

        {/* Main content */}
        <section className="flex-1 flex flex-col overflow-hidden">
          
          {/* Tab Switcher */}
          <div className="bg-white border-b border-slate-200 px-6 flex items-center gap-6">
            <button 
              onClick={() => setActiveTab('plot')}
              className={cn(
                "py-3 text-[10px] font-bold uppercase tracking-widest transition-all border-b-2",
                activeTab === 'plot' ? "border-[#ff6600] text-[#1c2b4a]" : "border-transparent text-slate-400 hover:text-slate-600"
              )}
            >
              <div className="flex items-center gap-2">
                <Activity size={14} /> Signal Plot
              </div>
            </button>
            <button 
              onClick={() => setActiveTab('table')}
              className={cn(
                "py-3 text-[10px] font-bold uppercase tracking-widest transition-all border-b-2",
                activeTab === 'table' ? "border-[#ff6600] text-[#1c2b4a]" : "border-transparent text-slate-400 hover:text-slate-600"
              )}
            >
              <div className="flex items-center gap-2">
                <TableIcon size={14} /> Data Grid
              </div>
            </button>
          </div>

          {activeTab === 'plot' ? (
            /* Chart */
            <div className="flex-1 bg-white p-4 flex flex-col min-h-0" ref={chartRef}>
              <div className="flex items-center justify-between mb-4">
                <h2 className="text-sm font-bold flex items-center gap-2 text-slate-700">
                  <Activity size={18} className="text-[#ff6600]"/> Interactive Signal Plot
                </h2>
                <div className="flex items-center gap-4 text-[10px] text-slate-400">
                  <span className="flex items-center gap-1"><ZoomIn size={12}/> Right-click + drag to zoom</span>
                  <span className="flex items-center gap-1"><Hand size={12}/> Left-click + drag to pan</span>
                </div>
              </div>

              <div className="mb-3">
                <button onClick={resetView}
                  className="inline-flex items-center gap-2 py-2 px-4 bg-white border border-slate-300 rounded-lg text-xs font-bold text-slate-700 hover:bg-slate-100 transition-all">
                  <RefreshCw size={14}/> Reset Plot View
                </button>
              </div>

              <div
                ref={containerRef}
                className="flex-1 min-h-0 relative select-none"
                style={{ cursor }}
              >
                {data.length > 0 && containerSize.width > 0 ? (
                  <LineChart 
                    width={containerSize.width} 
                    height={containerSize.height} 
                    data={data} 
                    margin={margins}
                  >
                    <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="#f1f5f9" />
                    <XAxis
                      dataKey={timeColumn}
                      domain={[left, right]}
                      type="number"
                      allowDataOverflow={true}
                      tick={{ fontSize: 10, fill: '#94a3b8' }}
                      tickFormatter={(val) => (typeof val === 'number' ? val.toFixed(2) : val)}
                      label={{ value: timeColumn, position: 'insideBottomRight', offset: -5, fontSize: 10, fill: '#64748b' }}
                    />
                    {axisConfigs.map(ax => (
                      <YAxis
                        key={ax.id}
                        yAxisId={ax.id}
                        orientation={ax.orientation}
                        tick={{ fontSize: 10, fill: '#94a3b8' }}
                        width={AXIS_W}
                        offset={ax.offset}
                        allowDataOverflow={true}
                        domain={['auto', 'auto']}
                        label={{
                          value: ax.label,
                          angle: ax.orientation === 'left' ? -90 : 90,
                          position: ax.orientation === 'left' ? 'insideLeft' : 'insideRight',
                          style: { textAnchor: 'middle', fill: '#64748b', fontSize: 9, fontWeight: 'bold' },
                          offset: 10,
                        }}
                      />
                    ))}
                    <Tooltip
                      cursor={{ stroke: '#ff6600', strokeWidth: 2, strokeDasharray: '3 3' }}
                      contentStyle={{
                        backgroundColor: '#1c2b4a', border: 'none', borderRadius: '8px',
                        color: '#fff', fontSize: '11px', boxShadow: '0 10px 15px -3px rgba(0,0,0,0.1)',
                      }}
                      itemStyle={{ color: '#fff' }}
                    />
                    <Legend
                      verticalAlign="top" align="right" iconType="circle"
                      wrapperStyle={{ fontSize: '10px', fontWeight: 'bold', textTransform: 'uppercase', paddingBottom: '20px' }}
                    />
                    {selectedSignals.map((sig, idx) => (
                      <Line
                        key={sig} type="monotone" dataKey={sig}
                        yAxisId={sigUnit[sig]}
                        stroke={SIGNAL_COLORS[idx % SIGNAL_COLORS.length]}
                        strokeWidth={2} dot={false}
                        activeDot={{ r: 6, strokeWidth: 2, stroke: '#fff' }}
                        animationDuration={300}
                      />
                    ))}
                    {zoomBox && (
                      <ReferenceArea
                        x1={zoomBox.x1}
                        x2={zoomBox.x2}
                        yAxisId={axisConfigs[0]?.id}
                        stroke="#ff6600"
                        strokeOpacity={1}
                        strokeWidth={2}
                        fill="#ff6600"
                        fillOpacity={0.5}
                        isFront={true}
                        label={{
                          value: `${Math.abs(zoomBox.x2 - zoomBox.x1).toFixed(3)}s`,
                          position: 'top',
                          fill: '#ff6600',
                          fontSize: 14,
                          fontWeight: 'bold',
                          offset: 25
                        }}
                        {...({} as any)}
                      />
                    )}
                  </LineChart>
                ) : data.length > 0 ? (
                  <div className="w-full h-full flex items-center justify-center bg-slate-50 rounded-2xl border border-slate-100">
                    <p className="text-slate-400 text-xs italic">Initializing plot area...</p>
                  </div>
                ) : (
                  <div className="w-full h-full flex items-center justify-center bg-slate-50 rounded-2xl border border-slate-100">
                    <div className="text-center">
                      <div className="w-16 h-16 bg-white rounded-full shadow-sm flex items-center justify-center mx-auto mb-4">
                        <FileUp className="text-slate-300" />
                      </div>
                      <p className="text-slate-400 text-xs font-medium">Waiting for data input...</p>
                    </div>
                  </div>
                )}
              </div>
            </div>
          ) : (
            /* Table */
            <div className="flex-1 bg-white flex flex-col min-h-0">
              <div className="px-6 py-3 border-b border-slate-100 flex items-center justify-between bg-slate-50/50">
                <h2 className="text-[10px] font-bold uppercase tracking-widest text-slate-500 flex items-center gap-2">
                  <TableIcon size={14}/> Data Inspection Grid
                </h2>
                {data.length>0&&(
                  <span className="text-[10px] font-mono text-slate-400">
                    Showing first 100 of {data.length} records
                  </span>
                )}
              </div>
              <div className="flex-1 overflow-auto">
                {data.length>0 ? (
                  <table className="w-full text-left border-collapse">
                    <thead className="sticky top-0 bg-white shadow-sm z-10">
                      <tr>
                        {metadata?.columns.map(col=>(
                          <th key={col} className="px-4 py-2 text-[10px] font-bold text-slate-400 uppercase border-b border-slate-100 bg-white">
                            {col}
                          </th>
                        ))}
                      </tr>
                    </thead>
                    <tbody>
                      {data.slice(0,100).map((row,i)=>(
                        <tr key={i} className="hover:bg-slate-50 transition-colors">
                          {metadata?.columns.map(col=>(
                            <td key={col} className="px-4 py-1.5 text-xs font-mono text-slate-600 border-b border-slate-50">
                              {row[col]!==null?row[col]:'-'}
                            </td>
                          ))}
                        </tr>
                      ))}
                    </tbody>
                  </table>
                ):(
                  <div className="h-full flex items-center justify-center text-slate-300 italic text-xs">
                    No records to display
                  </div>
                )}
              </div>
            </div>
          )}
        </section>
      </main>

      {/* Footer */}
      <footer className="bg-white border-t border-slate-200 px-6 py-2 flex items-center justify-between text-[10px] font-medium text-slate-400">
        <div className="flex items-center gap-4">
          <span className="flex items-center gap-1 text-emerald-600">
            <div className="w-1.5 h-1.5 rounded-full bg-emerald-500 animate-pulse"/>
            System Ready
          </span>
          <span className="border-l border-slate-200 pl-4">v1.6.0-forensic</span>
        </div>
        <div>&copy; {new Date().getFullYear()} Politie Academie - Forensic Engineering Unit</div>
      </footer>
    </div>
  );
}
