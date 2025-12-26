#pragma once
#include <pgmspace.h>

const char WEB_UI[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width,initial-scale=1"/>
  <title>ESP32 Arm - Action Recorder</title>
  <style>
    body{font-family:system-ui,Arial;margin:14px;max-width:980px}
    h2{margin:6px 0 10px}
    .small{color:#666;font-size:12px}
    .ok{color:#0a7} .bad{color:#b00}
    .row{display:flex;gap:10px;align-items:center;margin:8px 0}
    .row label{width:88px}
    input[type=range]{flex:1}
    .btns{display:flex;gap:10px;flex-wrap:wrap;margin:12px 0}
    button{
      padding:10px 12px;border:1px solid #ddd;border-radius:10px;background:#fafafa;
      touch-action:manipulation; user-select:none;
    }
    button.danger{border-color:#f2b; color:#b00}
    .grid{display:grid;grid-template-columns:1fr 1fr;gap:14px;align-items:start}
    @media (max-width:900px){.grid{grid-template-columns:1fr}}
    .card{border:1px solid #eee;border-radius:14px;padding:12px;background:#fff}
    .mono{font-family:ui-monospace,Consolas,monospace}
    input[type=text], input[type=number]{
      padding:8px 10px;border:1px solid #ddd;border-radius:10px;min-width:160px
    }
    table{width:100%;border-collapse:collapse}
    td,th{border-bottom:1px solid #eee;padding:6px 6px;text-align:left;font-size:13px}
    tr:hover{background:#fafafa}
    .pill{display:inline-block;padding:2px 8px;border:1px solid #eee;border-radius:999px;font-size:12px;color:#666}
    .full{grid-column:1 / -1;}
    .jogGrid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
    @media (max-width:900px){.jogGrid{grid-template-columns:1fr}}
    .jogBox{border:1px solid #eee;border-radius:12px;padding:10px}
    .jogBox h4{margin:0 0 8px}
  </style>
</head>
<body>
  <h2>机械臂遥控 + 动作记忆</h2>
  <div class="small">
    连接热点后打开 <b>192.168.4.1</b>　
    WebSocket：<span id="wsState" class="bad">DISCONNECTED</span>
    <span class="pill" id="playState">IDLE</span>
  </div>

  <div class="grid">
    <!-- 左：实时遥控 -->
    <div class="card">
      <h3 style="margin:4px 0 10px;">实时控制</h3>
      <div class="btns">
        <button onclick="sendCmd({cmd:'home'})">Home</button>
        <button onclick="sendCmd({cmd:'ready'})">Ready</button>
        <button onclick="gripOpen()">Open (S6=150)</button>
        <button onclick="gripClose()">Close (S6=50)</button>
        <button onclick="applyPose()">Apply Pose</button>
        <button class="danger" onclick="sendCmd({cmd:'act_stop'})">STOP</button>
      </div>

      <div id="sliders"></div>
      <p class="small">
        提示：S6 已限制在 50..150。若舵机啸叫=顶死，立刻缩小限位或回 Home。
      </p>
    </div>

    <!-- 右：动作记忆 -->
    <div class="card">
      <h3 style="margin:4px 0 10px;">动作记忆（关键帧）</h3>

      <div class="row">
        <label>动作名</label>
        <input id="actName" type="text" placeholder="例如: pick_1">
        <label style="width:auto">帧停留(ms)</label>
        <input id="holdMs" type="number" value="300" min="0" max="5000" style="width:110px">
      </div>

      <div class="btns">
        <button onclick="newAction()">新建动作</button>
        <button onclick="addFrame()">记录一帧</button>
        <button onclick="undoFrame()">撤销上一帧</button>
        <button onclick="saveAction()">保存到动作库</button>
      </div>

      <div class="small mono">当前帧数：<span id="frameCnt">0</span></div>
      <div style="max-height:180px;overflow:auto;margin-top:8px;border:1px solid #eee;border-radius:12px;">
        <table>
          <thead><tr><th>#</th><th>hold</th><th>pose(6轴)</th><th>操作</th></tr></thead>
          <tbody id="frameTable"></tbody>
        </table>
      </div>

      <hr style="border:none;border-top:1px solid #eee;margin:14px 0">

      <div class="row" style="justify-content:space-between;">
        <div>
          <b>动作库</b> <span class="small">(保存在ESP32里)</span>
        </div>
        <button onclick="refreshLibrary()">刷新</button>
      </div>

      <div style="max-height:220px;overflow:auto;margin-top:6px;border:1px solid #eee;border-radius:12px;">
        <table>
          <thead><tr><th>name</th><th>操作</th></tr></thead>
          <tbody id="libTable"></tbody>
        </table>
      </div>

      <p class="small">
        说明：保存/加载用 JSON。动作执行在 ESP32 端非阻塞播放；你可以随时 STOP。
      </p>
    </div>

    <!-- ✅ 新增：Jog / 点动 -->
    <div class="card full">
      <h3 style="margin:4px 0 6px;">Jog / 点动（按住连续）</h3>
      <div class="small">
        这页只负责“点动发命令”，ESP32 端需支持：<span class="mono">{"cmd":"jog","a":"yaw","dir":1,"step":2}</span>
      </div>

      <div class="row" style="margin-top:10px;">
        <label>Step(°)</label>
        <input id="jogStep" type="number" value="2" min="1" max="15" style="width:110px">
        <label style="width:auto">Period(ms)</label>
        <input id="jogPeriod" type="number" value="80" min="40" max="300" style="width:120px">
        <span class="small">（越小越快）</span>
      </div>

      <div class="jogGrid">
        <div class="jogBox">
          <h4>底座 Yaw</h4>
          <div class="btns">
            <button id="btnYawL">Yaw -</button>
            <button id="btnYawR">Yaw +</button>
          </div>
          <div class="small">用于左右旋转底座（S1）。</div>
        </div>

        <div class="jogBox">
          <h4>末端感联动</h4>
          <div class="btns">
            <button id="btnReachBack">后退</button>
            <button id="btnReachFwd">前进</button>
            <button id="btnLiftDown">下降</button>
            <button id="btnLiftUp">上升</button>
          </div>
          <div class="small">不做 IK，只做“肩+肘”联动。方向不对就在 ESP32 端改符号。</div>
        </div>

        <div class="jogBox">
          <h4>手腕</h4>
          <div class="btns">
            <button id="btnPitchDn">俯</button>
            <button id="btnPitchUp">仰</button>
            <button id="btnRollL">Roll -</button>
            <button id="btnRollR">Roll +</button>
          </div>
          <div class="small">俯仰=S4，滚转=S5（按你的映射）。</div>
        </div>

        <div class="jogBox">
          <h4>夹爪</h4>
          <div class="btns">
            <button id="btnGripClose">合</button>
            <button id="btnGripOpen">开</button>
          </div>
          <div class="small">夹爪=S6（你已限制 50..150）。</div>
        </div>
      </div>

      <p class="small" style="margin-top:10px;">
        操作习惯：按住按钮=持续点动；松开=停止。若浏览器偶发“卡住”，点一下空白处或切换标签即可。
      </p>
    </div>
  </div>


<script>
  const N=6;
  const names=["S1 Base","S2 Pitch1","S3 Pitch2","S4 Pitch3","S5 Roll","S6 Gripper"];
  let pose=[88,0,180,180,90,90];


  // 录制缓存（网页端）
  let frames=[]; // {p:[...], hold:ms}

  // 动作库列表（从ESP32取）
  let lib=[];

  let ws;

  function setWSState(ok){
    const el=document.getElementById('wsState');
    el.textContent=ok?"CONNECTED":"DISCONNECTED";
    el.className=ok?"ok":"bad";
  }
  function setPlayState(s){
    document.getElementById('playState').textContent=s;
  }

  // ---------------- Jog / 点动（网页端） ----------------
  let jogTimer=null;

  function jogStart(axis, dir){
    const step = parseInt(document.getElementById('jogStep').value || '2');
    const period = parseInt(document.getElementById('jogPeriod').value || '80');

    // 先来一次立即响应
    sendCmd({cmd:'jog', a:axis, dir:dir, step:step});

    // 按住连续发
    jogStop();
    jogTimer = setInterval(()=>{
      sendCmd({cmd:'jog', a:axis, dir:dir, step:step});
    }, period);
  }

  function jogStop(){
    if(jogTimer){ clearInterval(jogTimer); jogTimer=null; }
  }

  // 让按钮支持鼠标/触屏：pointerdown 开始，pointerup/leave/cancel 停止
  function bindJogBtn(id, axis, dir){
    const el = document.getElementById(id);
    if(!el) return;
    el.addEventListener('pointerdown', (e)=>{ e.preventDefault(); jogStart(axis, dir); });
    el.addEventListener('pointerup',   (e)=>{ e.preventDefault(); jogStop(); });
    el.addEventListener('pointerleave',(e)=>{ e.preventDefault(); jogStop(); });
    el.addEventListener('pointercancel',(e)=>{ e.preventDefault(); jogStop(); });
  }

  function bindAllJogBtns(){
    bindJogBtn('btnYawL','yaw',-1);
    bindJogBtn('btnYawR','yaw',+1);

    bindJogBtn('btnReachBack','reach',-1);
    bindJogBtn('btnReachFwd','reach',+1);

    bindJogBtn('btnLiftDown','lift',-1);
    bindJogBtn('btnLiftUp','lift',+1);

    bindJogBtn('btnPitchDn','pitch',-1);
    bindJogBtn('btnPitchUp','pitch',+1);

    bindJogBtn('btnRollL','roll',-1);
    bindJogBtn('btnRollR','roll',+1);

    bindJogBtn('btnGripClose','grip',-1);
    bindJogBtn('btnGripOpen','grip',+1);
  }

  function connect(){
    ws = new WebSocket(`ws://${location.hostname}:81/`);

    ws.onopen = () => {
      setWSState(true);
      sendCmd({cmd:'get'});
      refreshLibrary();
    };

    ws.onclose = () => {
      setWSState(false);
      jogStop();
      setTimeout(connect, 800);
    };

    ws.onerror = () => {
      setWSState(false);
    };

    ws.onmessage = (e) => {
      let msg;
      try { msg = JSON.parse(e.data); } catch (_) { return; }

      // 1) 实时姿态
      if (msg.type === 'pose' && Array.isArray(msg.p) && msg.p.length === 6) {
        pose = msg.p.map(x => parseInt(x));
        refreshUI();
        return;
      }

      // 2) 动作库列表
      if (msg.type === 'act_list') {
        lib = msg.list || [];
        renderLibrary();
        return;
      }

      // 3) 状态提示
      if (msg.type === 'status') {
        console.log(msg.msg);
        return;
      }

      // 4) 播放状态
      if (msg.type === 'play') {
        setPlayState(msg.state || 'IDLE');
        return;
      }

      // 5) ✅ 加载动作到编辑器（关键帧）
      if (msg.type === 'act' && Array.isArray(msg.frames)) {
        frames = msg.frames.map(fr => ({
          p: (fr.p || [90,90,90,90,90,90]).map(x => parseInt(x)),
          hold: parseInt(fr.hold || 0)
        }));
        document.getElementById('actName').value = msg.name || '';
        renderFrames();
        return;
      }
    };
  }

  function sendCmd(obj){
    if(ws && ws.readyState===1) ws.send(JSON.stringify(obj));
  }

  function buildSliders(){
    const box=document.getElementById('sliders');
    box.innerHTML='';
    for(let i=0;i<N;i++){
      const wrap=document.createElement('div');
      wrap.className='row';
      wrap.innerHTML=`
        <label>${names[i]}</label>
        <input id="r${i}" type="range" min="0" max="180" value="${pose[i]}" step="1">
        <span id="v${i}" style="width:42px;text-align:right">${pose[i]}</span>`;
      box.appendChild(wrap);

      const r=wrap.querySelector(`#r${i}`);
      r.oninput=()=>{
        const val=parseInt(r.value);
        pose[i]=val;
        document.getElementById('v'+i).textContent=val;
        sendCmd({cmd:'j', i, deg: val});
      };
    }
    // S6 更安全：限制 50..150
    const r5=document.getElementById('r5');
    r5.min=50; r5.max=150; r5.value=pose[5];
    document.getElementById('v5').textContent=pose[5];
  }

  function refreshUI(){
    for(let i=0;i<N;i++){
      const r=document.getElementById('r'+i);
      const v=document.getElementById('v'+i);
      if(r) r.value=pose[i];
      if(v) v.textContent=pose[i];
    }
  }

  function applyPose(){
    sendCmd({cmd:'p', p:pose});
  }

  function gripOpen(){ pose[5]=150; refreshUI(); sendCmd({cmd:'j', i:5, deg:150}); }
  function gripClose(){ pose[5]=50; refreshUI(); sendCmd({cmd:'j', i:5, deg:50}); }

  // ----------- 录制 -----------
  function newAction(){
    frames=[];
    renderFrames();
  }

  function addFrame(){
    const hold=parseInt(document.getElementById('holdMs').value||"0");
    frames.push({p:[...pose], hold:isNaN(hold)?0:hold});
    renderFrames();
  }

  function undoFrame(){
    frames.pop();
    renderFrames();
  }

  function renderFrames(){
    document.getElementById('frameCnt').textContent=frames.length;
    const tb=document.getElementById('frameTable');
    tb.innerHTML='';
    frames.forEach((f,idx)=>{
      const tr=document.createElement('tr');
      tr.innerHTML=`
        <td>${idx}</td>
        <td>${f.hold}</td>
        <td class="mono">${f.p.join(',')}</td>
        <td>
          <button onclick="applyFrame(${idx})">预览</button>
          <button class="danger" onclick="delFrame(${idx})">删</button>
        </td>`;
      tb.appendChild(tr);
    });
  }

  function applyFrame(idx){
    const f=frames[idx];
    pose=[...f.p];
    refreshUI();
    sendCmd({cmd:'p', p:pose});
  }

  function delFrame(idx){
    frames.splice(idx,1);
    renderFrames();
  }

  function sleep(ms){ return new Promise(r=>setTimeout(r, ms)); }

  async function saveAction(){
    const name = (document.getElementById('actName').value || '').trim();
    if(!name){ alert('请输入动作名'); return; }
    if(frames.length===0){ alert('还没有帧'); return; }

    // 1) 开始保存
    sendCmd({cmd:'act_save_begin', name, count: frames.length});

    // 2) 一帧一帧发送
    for(let i=0;i<frames.length;i++){
      const fr = frames[i];
      sendCmd({
        cmd:'act_save_frame',
        name,
        idx:i,
        p: fr.p,
        hold: fr.hold
      });
      await sleep(6);
    }

    // 3) 结束保存
    sendCmd({cmd:'act_save_end', name});

    // 4) 稍等后刷新动作库
    await sleep(200);
    refreshLibrary();
  }

  // ----------- 动作库 -----------
  function refreshLibrary(){
    sendCmd({cmd:'act_list'});
  }

  function renderLibrary(){
    const tb=document.getElementById('libTable');
    tb.innerHTML='';
    lib.forEach(name=>{
      const tr=document.createElement('tr');
      tr.innerHTML=`
        <td class="mono">${name}</td>
        <td>
          <button onclick="runAction('${name}')">执行</button>
          <button onclick="loadAction('${name}')">加载到编辑器</button>
          <button class="danger" onclick="delAction('${name}')">删除</button>
        </td>`;
      tb.appendChild(tr);
    });
  }

  function runAction(name){
    sendCmd({cmd:'act_run', name});
  }
  function delAction(name){
    if(!confirm('确定删除 '+name+' ?')) return;
    sendCmd({cmd:'act_delete', name});
    setTimeout(refreshLibrary, 300);
  }
  function loadAction(name){
    sendCmd({cmd:'act_load', name});
  }

  // 初始化
  buildSliders();
  renderFrames();
  bindAllJogBtns();
  connect();
</script>
</body>
</html>
)rawliteral";

