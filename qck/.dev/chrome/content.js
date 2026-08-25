(() => {
  if (globalThis.__qckNitInstalled) return;
  globalThis.__qckNitInstalled = true;

  const ids = new WeakMap();
  let nextId = 1;

  let enabled = false;
  let locked = false;
  let depthMode = false;
  let lastPoint = { x: innerWidth / 2, y: innerHeight / 2 };
  let currentLeaf = null;
  let currentChain = [];

  let host = null;
  let shadow = null;
  let layer = null;
  let hud = null;
  let depthButton = null;
  let stage = null;
  let scene = null;

  const camera = {
    rx: 54,
    ry: -18,
    rz: -8,
    z: -140
  };

  let drag = null;

  function idOf(el) {
    let id = ids.get(el);
    if (!id) {
      id = nextId++;
      ids.set(el, id);
    }
    return id;
  }

  function rectOf(el) {
    const r = el.getBoundingClientRect();
    return { x: r.x, y: r.y, w: r.width, h: r.height };
  }

  function visible(el) {
    if (!(el instanceof Element)) return false;

    const r = el.getBoundingClientRect();
    if (r.width <= 0 || r.height <= 0) return false;

    const s = getComputedStyle(el);
    return s.display !== "none" &&
           s.visibility !== "hidden" &&
           Number(s.opacity || 1) !== 0;
  }

  function parentElement(el) {
    const root = el.getRootNode();
    if (el.parentElement) return el.parentElement;
    if (root instanceof ShadowRoot) return root.host;
    return null;
  }

  function chainOf(el) {
    const chain = [];
    let node = el;

    while (node instanceof Element) {
      if (visible(node)) chain.push(node);
      node = parentElement(node);
    }

    return chain.reverse();
  }

  function meaningfulChain(chain, leaf) {
    const viewportArea = Math.max(1, innerWidth * innerHeight);

    let out = chain.filter((el) => {
      if (el === leaf) return true;
      if (el === document.documentElement || el === document.body) return false;

      const r = el.getBoundingClientRect();
      const area = Math.max(0, r.width) * Math.max(0, r.height);

      // Do not let full-page wrappers dominate the depth scene.
      return area / viewportArea < 0.88;
    });

    if (!out.includes(leaf)) out.push(leaf);

    // Keep the local structural neighborhood. The page can be much deeper.
    out = out.slice(-7);

    // A single leaf is not much of a depth scene; add its nearest visible parent.
    if (out.length === 1) {
      const parent = parentElement(leaf);
      if (parent && visible(parent)) out.unshift(parent);
    }

    return out;
  }

  function selectorHint(el) {
    if (!el) return "";
    if (el.id) return `${el.tagName.toLowerCase()}#${el.id}`;

    const cls = [...el.classList]
      .filter(Boolean)
      .slice(0, 2)
      .join(".");

    return cls
      ? `${el.tagName.toLowerCase()}.${cls}`
      : el.tagName.toLowerCase();
  }

  function ensureUi() {
    if (host) return;

    host = document.createElement("div");
    host.dataset.qckNit = "overlay";
    host.style.cssText = [
      "position:fixed",
      "inset:0",
      "z-index:2147483647",
      "pointer-events:none",
      "contain:strict"
    ].join(";");

    shadow = host.attachShadow({ mode: "closed" });
    shadow.innerHTML = `
      <style>
        :host { all: initial; }

        #layer {
          position: fixed;
          inset: 0;
          pointer-events: none;
          overflow: hidden;
        }

        .box {
          position: fixed;
          box-sizing: border-box;
          border: 1px solid rgba(0,220,255,.78);
          background: rgba(0,220,255,.035);
        }

        .box::after {
          content: attr(data-label);
          position: absolute;
          left: 0;
          top: -18px;
          max-width: 320px;
          overflow: hidden;
          text-overflow: ellipsis;
          white-space: nowrap;
          padding: 1px 5px;
          border-radius: 4px;
          background: rgba(8,12,18,.88);
          color: #dffbff;
          font: 11px/15px ui-monospace,SFMono-Regular,Consolas,monospace;
        }

        .leaf {
          border-width: 2px;
          background: rgba(255,210,60,.07);
          border-color: rgba(255,210,60,.95);
        }

        .leaf::after {
          background: rgba(55,42,0,.92);
          color: #fff4b0;
        }

        #hud {
          position: fixed;
          right: 12px;
          top: 12px;
          min-width: 260px;
          max-width: 420px;
          padding: 9px 11px;
          border: 1px solid rgba(255,255,255,.16);
          border-radius: 8px;
          background: rgba(7,10,14,.92);
          color: #eef7ff;
          box-shadow: 0 8px 34px rgba(0,0,0,.28);
          font: 12px/1.45 ui-monospace,SFMono-Regular,Consolas,monospace;
        }

        #hud b {
          color: #7ee9ff;
          font-weight: 600;
        }

        #hud .muted {
          opacity: .68;
        }

        #depthButton {
          position: fixed;
          right: 12px;
          top: 122px;
          pointer-events: auto;
          cursor: pointer;
          border: 1px solid rgba(126,233,255,.55);
          border-radius: 999px;
          padding: 7px 11px;
          background: rgba(7,10,14,.92);
          color: #dffbff;
          box-shadow: 0 8px 30px rgba(0,0,0,.25);
          font: 700 12px/1 ui-monospace,SFMono-Regular,Consolas,monospace;
        }

        #depthButton[data-active="1"] {
          border-color: rgba(255,210,60,.95);
          color: #fff4b0;
        }

        #stage {
          position: fixed;
          inset: 0;
          display: none;
          overflow: hidden;
          perspective: 1200px;
          perspective-origin: 50% 48%;
          background:
            radial-gradient(circle at 50% 45%, rgba(0,220,255,.06), transparent 55%),
            rgba(0,0,0,.12);
          pointer-events: none;
          touch-action: none;
        }

        #stage[data-active="1"] {
          display: block;
          pointer-events: auto;
          cursor: grab;
        }

        #stage[data-dragging="1"] {
          cursor: grabbing;
        }

        #scene {
          position: absolute;
          inset: 0;
          transform-style: preserve-3d;
          transform-origin: 50% 50%;
          transition: transform 120ms ease-out;
        }

        .plane {
          position: absolute;
          box-sizing: border-box;
          transform-style: preserve-3d;
          border: 1px solid rgba(0,220,255,.88);
          background: rgba(4,34,42,.24);
          box-shadow:
            0 0 0 1px rgba(255,255,255,.025) inset,
            0 12px 35px rgba(0,0,0,.16);
          backface-visibility: visible;
        }

        .plane.leaf-plane {
          border: 2px solid rgba(255,210,60,.98);
          background: rgba(80,58,0,.28);
        }

        .plane-label {
          position: absolute;
          left: 0;
          top: -19px;
          padding: 2px 6px;
          border-radius: 4px;
          color: #dffbff;
          background: rgba(8,12,18,.92);
          white-space: nowrap;
          font: 11px/15px ui-monospace,SFMono-Regular,Consolas,monospace;
          transform: translateZ(1px);
        }

        .leaf-plane .plane-label {
          color: #fff4b0;
          background: rgba(55,42,0,.94);
        }

        #depthHelp {
          position: fixed;
          left: 50%;
          bottom: 18px;
          transform: translateX(-50%);
          display: none;
          padding: 7px 10px;
          border: 1px solid rgba(255,255,255,.14);
          border-radius: 999px;
          background: rgba(7,10,14,.88);
          color: rgba(238,247,255,.8);
          font: 11px/1 ui-monospace,SFMono-Regular,Consolas,monospace;
          pointer-events: none;
        }

        #stage[data-active="1"] + #depthHelp {
          display: block;
        }
      </style>

      <div id="layer"></div>
      <div id="stage">
        <div id="scene"></div>
      </div>
      <div id="depthHelp">drag rotate · wheel depth · nit' returns 2D</div>
      <div id="hud"></div>
      <button id="depthButton" type="button">nit' 3D</button>
    `;

    layer = shadow.querySelector("#layer");
    hud = shadow.querySelector("#hud");
    depthButton = shadow.querySelector("#depthButton");
    stage = shadow.querySelector("#stage");
    scene = shadow.querySelector("#scene");

    depthButton.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();
      toggleDepth();
    });

    stage.addEventListener("pointerdown", onDepthPointerDown);
    stage.addEventListener("pointermove", onDepthPointerMove);
    stage.addEventListener("pointerup", onDepthPointerUp);
    stage.addEventListener("pointercancel", onDepthPointerUp);
    stage.addEventListener("wheel", onDepthWheel, { passive: false });

    document.documentElement.appendChild(host);
  }

  function clearInspect() {
    if (layer) layer.replaceChildren();
  }

  function updateCamera() {
    if (!scene) return;

    scene.style.transform = [
      `translateZ(${camera.z}px)`,
      `rotateX(${camera.rx}deg)`,
      `rotateY(${camera.ry}deg)`,
      `rotateZ(${camera.rz}deg)`
    ].join(" ");
  }

  function renderDepth() {
    ensureUi();
    clearInspect();

    if (!currentLeaf || !currentChain.length) {
      depthMode = false;
      return;
    }

    const chain = meaningfulChain(currentChain, currentLeaf);
    scene.replaceChildren();

    chain.forEach((el, index) => {
      const r = rectOf(el);
      const plane = document.createElement("div");
      const isLeaf = el === currentLeaf;

      plane.className = `plane${isLeaf ? " leaf-plane" : ""}`;
      plane.style.left = `${r.x}px`;
      plane.style.top = `${r.y}px`;
      plane.style.width = `${r.w}px`;
      plane.style.height = `${r.h}px`;

      // The actual page remains untouched. Only the qck.nit copy receives Z.
      plane.style.transform = `translateZ(${index * 58}px)`;

      const label = document.createElement("div");
      label.className = "plane-label";
      label.textContent = `${idOf(el)} · z${index} · ${selectorHint(el)}`;
      plane.appendChild(label);

      scene.appendChild(plane);
    });

    updateCamera();

    stage.dataset.active = "1";
    depthButton.dataset.active = "1";
    depthButton.textContent = "nit' 2D";

    const leafRect = rectOf(currentLeaf);
    hud.innerHTML = `
      <b>qck.nit depth</b><br>
      id=${idOf(currentLeaf)} planes=${chain.length} ${selectorHint(currentLeaf)}<br>
      box=[${leafRect.x.toFixed(1)}, ${leafRect.y.toFixed(1)}, ${leafRect.w.toFixed(1)}, ${leafRect.h.toFixed(1)}]<br>
      <span class="muted">page untouched · local depth projection</span>`;

    console.debug("qck.nit depth", {
      leaf: idOf(currentLeaf),
      planes: chain.map((el, index) => ({
        id: idOf(el),
        z: index,
        box: rectOf(el),
        selector: selectorHint(el)
      }))
    });
  }

  function leaveDepth() {
    depthMode = false;
    locked = false;
    drag = null;

    if (scene) scene.replaceChildren();

    if (stage) {
      stage.dataset.active = "0";
      stage.dataset.dragging = "0";
    }

    if (depthButton) {
      depthButton.dataset.active = "0";
      depthButton.textContent = "nit' 3D";
    }

    draw(lastPoint);
  }

  function toggleDepth() {
    if (!enabled) return;

    if (depthMode) {
      leaveDepth();
      return;
    }

    if (!currentLeaf) {
      const previousLock = locked;
      locked = false;
      draw(lastPoint);
      locked = previousLock;
    }

    if (!currentLeaf) return;

    depthMode = true;
    locked = true;
    renderDepth();
  }

  function draw(point = lastPoint) {
    if (!enabled || locked || depthMode) return;

    lastPoint = point;
    ensureUi();

    const stack = document.elementsFromPoint(point.x, point.y)
      .filter((el) => el !== host && visible(el));

    const leaf = stack[0];

    clearInspect();

    if (!leaf) {
      currentLeaf = null;
      currentChain = [];
      hud.textContent = "qck.nit — no visible box";
      return;
    }

    const chain = chainOf(leaf);
    currentLeaf = leaf;
    currentChain = chain;

    const capped = chain.slice(-10);

    capped.forEach((el, index) => {
      const r = rectOf(el);
      const depth = chain.indexOf(el);
      const box = document.createElement("div");

      box.className = `box${el === leaf ? " leaf" : ""}`;
      box.style.left = `${r.x}px`;
      box.style.top = `${r.y}px`;
      box.style.width = `${r.w}px`;
      box.style.height = `${r.h}px`;
      box.style.transform = `translate(${index * 1.5}px, ${index * 1.5}px)`;
      box.dataset.label = `${idOf(el)} · d${depth} · ${selectorHint(el)}`;

      layer.appendChild(box);
    });

    const r = rectOf(leaf);
    const hitIds = stack.slice(0, 64).map(idOf);

    let bitmap = 0n;
    stack.slice(0, 64).forEach((_, i) => {
      bitmap |= 1n << BigInt(i);
    });

    hud.innerHTML = `
      <b>qck.nit</b> ${locked ? "locked" : "live"}<br>
      id=${idOf(leaf)} depth=${chain.length - 1} ${selectorHint(leaf)}<br>
      box=[${r.x.toFixed(1)}, ${r.y.toFixed(1)}, ${r.w.toFixed(1)}, ${r.h.toFixed(1)}]<br>
      hit=${stack.length} bitmap=0x${bitmap.toString(16)}<br>
      <span class="muted">nit' → 3D · Alt+click lock · icon toggle</span>`;

    console.debug("qck.nit", {
      leaf: {
        id: idOf(leaf),
        depth: chain.length - 1,
        box: r,
        selector: selectorHint(leaf)
      },
      hitIds,
      bitmap: `0x${bitmap.toString(16)}`
    });
  }

  function onMove(event) {
    if (!enabled || locked || depthMode) return;

    requestAnimationFrame(() => {
      draw({ x: event.clientX, y: event.clientY });
    });
  }

  function onAltClick(event) {
    if (!enabled || depthMode || !event.altKey) return;

    event.preventDefault();
    event.stopPropagation();

    lastPoint = { x: event.clientX, y: event.clientY };
    locked = !locked;

    if (locked) {
      const wasLocked = locked;
      locked = false;
      draw(lastPoint);
      locked = wasLocked;

      if (hud) {
        hud.innerHTML = hud.innerHTML.replace("live", "locked");
      }
    } else {
      draw(lastPoint);
    }
  }

  function onDepthPointerDown(event) {
    if (!depthMode) return;

    drag = {
      id: event.pointerId,
      x: event.clientX,
      y: event.clientY,
      rx: camera.rx,
      ry: camera.ry
    };

    stage.setPointerCapture(event.pointerId);
    stage.dataset.dragging = "1";
  }

  function onDepthPointerMove(event) {
    if (!drag || drag.id !== event.pointerId) return;

    const dx = event.clientX - drag.x;
    const dy = event.clientY - drag.y;

    camera.ry = drag.ry + dx * 0.18;
    camera.rx = Math.max(8, Math.min(82, drag.rx - dy * 0.18));
    updateCamera();
  }

  function onDepthPointerUp(event) {
    if (!drag || drag.id !== event.pointerId) return;

    drag = null;
    stage.dataset.dragging = "0";

    try {
      stage.releasePointerCapture(event.pointerId);
    } catch (_) {
      // Capture may already be gone.
    }
  }

  function onDepthWheel(event) {
    if (!depthMode) return;

    event.preventDefault();
    camera.z = Math.max(-700, Math.min(200, camera.z - event.deltaY * 0.45));
    updateCamera();
  }

  function toggle() {
    enabled = !enabled;
    locked = false;
    depthMode = false;
    currentLeaf = null;
    currentChain = [];
    drag = null;

    if (!enabled) {
      host?.remove();
      host = shadow = layer = hud = depthButton = stage = scene = null;
      return;
    }

    ensureUi();
    draw(lastPoint);
  }

  addEventListener("mousemove", onMove, { passive: true, capture: true });
  addEventListener("click", onAltClick, true);

  addEventListener("scroll", () => {
    if (enabled && !locked && !depthMode) draw(lastPoint);
  }, { passive: true, capture: true });

  addEventListener("resize", () => {
    if (!enabled) return;
    if (depthMode) renderDepth();
    else if (!locked) draw(lastPoint);
  }, { passive: true });

  addEventListener("keydown", (event) => {
    if (!enabled || !depthMode) return;
    if (event.key === "Escape") leaveDepth();
  }, true);

  chrome.runtime.onMessage.addListener((message) => {
    if (message?.type === "qck.nit.toggle") toggle();
  });
})();
