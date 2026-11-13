/*
 * Phantasia IV - 3D ceremonies (three.js / WebGL).
 *
 * play(kind, host) renders a ceremony behind the title card inside the
 * .arrival overlay `host` and returns { dispose }.
 *   kind 2      apprentice: a burst of smoke
 *   kind 3, 4   wizard: lightning storm over an arcane circle
 *   kind 101    steward: silk banners rippling on gold rods
 *   kind 102/3  king / queen: a jewelled gold crown in golden light
 *   kind 104    valar: the heavens open, rings of runes and stars
 *
 * Everything is built from geometry and canvas-drawn textures, so no
 * models or images need downloading.
 */
import * as THREE from "three";
import { RoomEnvironment } from "./vendor/three/addons/environments/RoomEnvironment.js";
import { EffectComposer } from "./vendor/three/addons/postprocessing/EffectComposer.js";
import { RenderPass } from "./vendor/three/addons/postprocessing/RenderPass.js";
import { UnrealBloomPass } from "./vendor/three/addons/postprocessing/UnrealBloomPass.js";
import { OutputPass } from "./vendor/three/addons/postprocessing/OutputPass.js";

const RUNES = "ᚠᚢᚦᚨᚱᚲᚷᚹᚺᚾᛁᛃᛇᛈᛉᛊᛏᛒᛖᛗᛚᛜᛞᛟ";

/* ------------------------------------------------------------ helpers */

function canvasTexture(w, h, draw) {
  const c = document.createElement("canvas");
  c.width = w; c.height = h;
  draw(c.getContext("2d"), w, h);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = 4;
  return t;
}

/* a glowing ring of runes, for the wizard's circle and the Valar */
function runeTexture(color) {
  return canvasTexture(1024, 1024, (g, w) => {
    const c = w / 2;
    g.clearRect(0, 0, w, w);
    g.strokeStyle = color; g.fillStyle = color;
    g.shadowColor = color; g.shadowBlur = 18;
    for (const [r, lw] of [[480, 6], [462, 2], [360, 4], [345, 1.5], [200, 3]]) {
      g.lineWidth = lw; g.beginPath(); g.arc(c, c, r, 0, Math.PI * 2); g.stroke();
    }
    g.font = "bold 58px serif"; g.textAlign = "center"; g.textBaseline = "middle";
    const glyphs = RUNES + RUNES.slice(0, 8);
    for (let i = 0; i < glyphs.length; i++) {
      const a = (i / glyphs.length) * Math.PI * 2;
      g.save(); g.translate(c + Math.cos(a) * 411, c + Math.sin(a) * 411); g.rotate(a + Math.PI / 2);
      g.fillText(glyphs[i], 0, 0); g.restore();
    }
    g.lineWidth = 2.5;                                  /* two triangles: a star */
    for (const off of [0, Math.PI]) {
      g.beginPath();
      for (let k = 0; k <= 3; k++) {
        const a = off - Math.PI / 2 + (k * Math.PI * 2) / 3;
        const x = c + Math.cos(a) * 345, y = c + Math.sin(a) * 345;
        k ? g.lineTo(x, y) : g.moveTo(x, y);
      }
      g.stroke();
    }
  });
}

function softDot(color = "#ffffff") {
  return canvasTexture(128, 128, (g, w) => {
    const r = g.createRadialGradient(w / 2, w / 2, 0, w / 2, w / 2, w / 2);
    r.addColorStop(0, color); r.addColorStop(0.25, color); r.addColorStop(1, "rgba(0,0,0,0)");
    g.fillStyle = r; g.fillRect(0, 0, w, w);
  });
}

/* fractal noise clouds */
function cloudTexture(tint) {
  return canvasTexture(512, 256, (g, w, h) => {
    const img = g.createImageData(w, h);
    const grid = (s) => { const a = []; for (let i = 0; i < (s + 1) * (s + 1); i++) a.push(Math.random()); return a; };
    const octaves = [4, 8, 16, 32].map((s) => ({ s, v: grid(s) }));
    const sample = (o, x, y) => {
      const fx = (x / w) * o.s, fy = (y / h) * o.s, ix = Math.floor(fx), iy = Math.floor(fy);
      const tx = fx - ix, ty = fy - iy, S = o.s + 1, v = o.v;
      const a = v[iy * S + ix], b = v[iy * S + ix + 1], c = v[(iy + 1) * S + ix], d = v[(iy + 1) * S + ix + 1];
      const sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
      return (a * (1 - sx) + b * sx) * (1 - sy) + (c * (1 - sx) + d * sx) * sy;
    };
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      let n = 0, amp = 0.55;
      for (const o of octaves) { n += sample(o, x, y) * amp; amp *= 0.5; }
      n = Math.max(0, Math.min(1, (n - 0.35) * 1.8));
      const i = (y * w + x) * 4;
      img.data[i] = tint[0] * n; img.data[i + 1] = tint[1] * n; img.data[i + 2] = tint[2] * n; img.data[i + 3] = 255;
    }
    g.putImageData(img, 0, 0);
  });
}

/* a wisp of smoke: noise faded to a soft round edge */
function puffTexture() {
  const noise = cloudTexture([255, 255, 255]).image;
  return canvasTexture(256, 256, (g, w) => {
    g.drawImage(noise, 0, 0, 256, 256);
    const img = g.getImageData(0, 0, w, w);
    for (let y = 0; y < w; y++) for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 4, d = Math.hypot(x - w / 2, y - w / 2) / (w / 2);
      const fall = Math.max(0, 1 - d * d);
      img.data[i + 3] = Math.min(255, img.data[i] * 1.4 * fall);
      img.data[i] = img.data[i + 1] = img.data[i + 2] = 230;
    }
    g.putImageData(img, 0, 0);
  });
}

function gold(rough = 0.22) {
  return new THREE.MeshStandardMaterial({ color: 0xffc94d, metalness: 1, roughness: rough });
}

function gem(color) {
  return new THREE.MeshPhysicalMaterial({
    color, metalness: 0, roughness: 0.04, clearcoat: 1, clearcoatRoughness: 0.03,
    emissive: color, emissiveIntensity: 0.25, flatShading: true, ior: 2.2, specularIntensity: 1,
  });
}

const ease = (t) => 1 - Math.pow(1 - Math.min(1, Math.max(0, t)), 3);

/* ------------------------------------------------------------ scenes */

function wizardScene(scene, camera, level, flash) {
  scene.background = new THREE.Color(0x07051a);
  scene.fog = new THREE.FogExp2(0x0a0820, 0.055);
  camera.position.set(0, 1.4, 9.5);
  camera.lookAt(0, 0.4, 0);

  /* the sky is a huge backdrop far behind the ground; its lower part fades
     to the fog colour, and the fog swallows the far ground, so the two
     meet in a soft misty horizon instead of a hard line */
  const HORIZON = "rgb(10,8,32)";
  const fade = (tex, color) => {
    const g = tex.image.getContext("2d"), h = tex.image.height, w = tex.image.width;
    const gr = g.createLinearGradient(0, h, 0, h * 0.55);
    gr.addColorStop(0, color); gr.addColorStop(0.45, color); gr.addColorStop(1, "rgba(0,0,0,0)");
    g.fillStyle = gr; g.fillRect(0, 0, w, h);
    tex.needsUpdate = true;
    return tex;
  };
  const clouds = fade(cloudTexture([150, 135, 235]), HORIZON);
  const glow = fade(cloudTexture([120, 110, 200]), "rgb(0,0,0)");
  const skyGeo = new THREE.PlaneGeometry(300, 120);
  const sky = new THREE.Mesh(skyGeo, new THREE.MeshBasicMaterial({ map: clouds, fog: false }));
  sky.position.set(0, 32, -90);
  scene.add(sky);
  /* the clouds light up with each strike (fades out above the horizon) */
  const skyMat = new THREE.MeshBasicMaterial({ map: glow, fog: false, transparent: true, opacity: 0,
    blending: THREE.AdditiveBlending, depthWrite: false, color: new THREE.Color(1.4, 1.5, 2.2) });
  const skyFlash = new THREE.Mesh(skyGeo, skyMat);
  skyFlash.position.set(0, 32, -89.9);
  scene.add(skyFlash);

  const ground = new THREE.Mesh(new THREE.CircleGeometry(120, 64),
    new THREE.MeshStandardMaterial({ color: 0x0c0b14, roughness: 0.32, metalness: 0.3, envMapIntensity: 0.05 }));
  ground.rotation.x = -Math.PI / 2; ground.position.y = -2;
  scene.add(ground);

  const circleMat = new THREE.MeshBasicMaterial({ map: runeTexture("#9fb8ff"), transparent: true,
    blending: THREE.AdditiveBlending, depthWrite: false, color: new THREE.Color(1.6, 1.8, 2.6) });
  const circle = new THREE.Mesh(new THREE.PlaneGeometry(9, 9), circleMat);
  circle.rotation.x = -Math.PI / 2; circle.position.y = -1.98;
  scene.add(circle);

  scene.add(new THREE.AmbientLight(0x4a4a8a, 0.35));
  const strike = new THREE.PointLight(0xc8d6ff, 0, 0, 0);
  scene.add(strike);

  /* rising sparks */
  const N = 220, pos = new Float32Array(N * 3), speed = [];
  for (let i = 0; i < N; i++) {
    const a = Math.random() * Math.PI * 2, r = Math.random() * 4.2;
    pos.set([Math.cos(a) * r, -2 + Math.random() * 6, Math.sin(a) * r], i * 3);
    speed.push(0.4 + Math.random() * 1.2);
  }
  const sparkGeo = new THREE.BufferGeometry();
  sparkGeo.setAttribute("position", new THREE.BufferAttribute(pos, 3));
  const sparks = new THREE.Points(sparkGeo, new THREE.PointsMaterial({ size: 0.09, map: softDot("#cfe0ff"),
    transparent: true, blending: THREE.AdditiveBlending, depthWrite: false, color: new THREE.Color(1.5, 1.7, 2.4) }));
  scene.add(sparks);

  /* a branching bolt from the sky to the circle, built from glowing rods */
  function bolt(x) {
    const group = new THREE.Group();
    const core = new THREE.MeshBasicMaterial({ color: new THREE.Color(1.9, 2.1, 3.2), fog: false });
    const rod = new THREE.CylinderGeometry(1, 1, 1, 8, 1, false);
    const knot = new THREE.SphereGeometry(1, 8, 6);       /* rounds off each bend */
    const seg = (a, b, w) => {
      const d = new THREE.Vector3().subVectors(b, a), len = d.length();
      const mesh = new THREE.Mesh(rod, core);
      mesh.scale.set(w, len, w);
      mesh.position.copy(a).addScaledVector(d, 0.5);
      mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), d.normalize());
      group.add(mesh);
      const k = new THREE.Mesh(knot, core);
      k.scale.setScalar(w); k.position.copy(b);
      group.add(k);
    };
    const path = (a, b, depth, w) => {
      let pts = [a, b];
      for (let k = 0; k < 6; k++) {
        const next = [pts[0]];
        for (let i = 1; i < pts.length; i++) {
          const p = pts[i - 1], q = pts[i], len = p.distanceTo(q);
          next.push(new THREE.Vector3((p.x + q.x) / 2 + (Math.random() - 0.5) * len * 0.45,
            (p.y + q.y) / 2, (p.z + q.z) / 2 + (Math.random() - 0.5) * len * 0.3), q);
        }
        pts = next;
      }
      for (let i = 1; i < pts.length; i++) {
        seg(pts[i - 1], pts[i], w);
        if (depth < 2 && Math.random() < (depth ? 0.03 : 0.05)) {
          const p = pts[i], dir = new THREE.Vector3((Math.random() - 0.5) * 3, -1.5 - Math.random() * 2, (Math.random() - 0.5));
          path(p, p.clone().add(dir), depth + 1, w * 0.55);
        }
      }
    };
    path(new THREE.Vector3(x, 9, -3), new THREE.Vector3(x * 0.15, -2, 0), 0, 0.075);
    return group;
  }

  /* strikes keep coming the whole time */
  const times = [0.12, 0.5, 1.15, 1.9, 2.7, 3.5, 4.3];
  const strikes = times.map((t, i) => ({ t, x: i === 0 ? 0 : (Math.random() - 0.5) * 9, group: null }));

  return (t, dt) => {
    camera.position.z = 9.5 - ease(t / 5) * 1.6;
    circle.rotation.z += dt * 0.25;
    let lit = 0;
    for (const s of strikes) {
      const age = t - s.t;
      const on = age > 0 && age < 0.45 && !(age > 0.1 && age < 0.16);
      if (on && !s.group) { s.group = bolt(s.x); scene.add(s.group); if (s === strikes[0]) flash(); }
      if (s.group) s.group.visible = on;
      if (on) { lit = Math.max(lit, 1 - age / 0.45); strike.position.set(s.x * 0.5, 3, 1); }
      if (age > 0.5 && s.group) { scene.remove(s.group); s.group = null; }
    }
    strike.intensity = lit * (level >= 4 ? 14 : 10);
    skyMat.opacity = lit * 0.8;
    circleMat.color.setRGB(1.6 + lit * 3, 1.8 + lit * 3, 2.6 + lit * 3);
    const p = sparkGeo.attributes.position;
    for (let i = 0; i < N; i++) {
      let y = p.getY(i) + speed[i] * dt;
      if (y > 5) y = -2;
      p.setY(i, y);
    }
    p.needsUpdate = true;
  };
}

function apprenticeScene(scene, camera) {
  scene.background = new THREE.Color(0x0b0b0f);
  camera.position.set(0, 0, 8); camera.lookAt(0, 0, 0);
  const tex = puffTexture();
  const puffs = [];
  for (let i = 0; i < 70; i++) {
    const m = new THREE.Sprite(new THREE.SpriteMaterial({ map: tex, transparent: true, depthWrite: false,
      color: new THREE.Color().setHSL(0.65, 0.05, 0.5 + Math.random() * 0.3), opacity: 0.0 }));
    const a = Math.random() * Math.PI * 2, e = Math.random() * Math.PI - Math.PI / 2;
    const v = new THREE.Vector3(Math.cos(a) * Math.cos(e), Math.sin(e) * 0.7, Math.sin(a) * Math.cos(e)).multiplyScalar(1 + Math.random() * 2.2);
    m.material.rotation = Math.random() * 6;
    puffs.push({ m, v, s: 0.6 + Math.random() * 1.4, spin: (Math.random() - 0.5) * 1.5 });
    scene.add(m);
  }
  return (t, dt) => {
    for (const p of puffs) {
      const k = Math.min(1, t / 2.4);
      p.m.position.copy(p.v).multiplyScalar(ease(k) * 1.6);
      p.m.scale.setScalar(p.s * (0.5 + k * 2.4));
      p.m.material.opacity = t < 0.25 ? t * 3 : Math.max(0, 0.75 * (1 - (t - 0.25) / 4.5));
      p.m.material.rotation += p.spin * dt;
    }
  };
}

function crownScene(scene, camera, renderer) {
  scene.background = new THREE.Color(0x0b0702);
  camera.position.set(0, 1.0, 7.2); camera.lookAt(0, 0.6, 0);

  const crown = new THREE.Group();
  const g = gold(0.18);

  const band = new THREE.Mesh(new THREE.CylinderGeometry(1, 1.04, 0.5, 96, 1, true), g);
  band.material.side = THREE.DoubleSide;
  crown.add(band);
  for (const y of [-0.25, 0.25]) {
    const rim = new THREE.Mesh(new THREE.TorusGeometry(1.02, 0.055, 16, 96), g);
    rim.rotation.x = Math.PI / 2; rim.position.y = y; crown.add(rim);
  }
  /* points with balls on top */
  for (let i = 0; i < 8; i++) {
    const a = (i / 8) * Math.PI * 2;
    const spike = new THREE.Mesh(new THREE.ConeGeometry(0.2, 0.75, 4), g);
    spike.position.set(Math.cos(a) * 1.0, 0.62, Math.sin(a) * 1.0);
    spike.rotation.y = -a; spike.scale.z = 0.45;
    crown.add(spike);
    const ball = new THREE.Mesh(new THREE.SphereGeometry(0.08, 24, 16), g);
    ball.position.set(Math.cos(a) * 1.0, 1.03, Math.sin(a) * 1.0);
    crown.add(ball);
  }
  /* velvet cap and arches */
  const cap = new THREE.Mesh(new THREE.SphereGeometry(0.97, 48, 24, 0, Math.PI * 2, 0, Math.PI / 2),
    new THREE.MeshPhysicalMaterial({ color: 0x5a0610, roughness: 0.9, sheen: 0.6, sheenColor: new THREE.Color(0xb02a3a), sheenRoughness: 0.6, envMapIntensity: 0.3 }));
  cap.scale.y = 0.85; cap.position.y = 0.2; crown.add(cap);
  for (const r of [0, Math.PI / 2]) {
    const arch = new THREE.Mesh(new THREE.TorusGeometry(1.0, 0.05, 12, 64, Math.PI), g);
    arch.rotation.y = r; arch.position.y = 0.22; arch.scale.y = 1.05; crown.add(arch);
  }
  const orb = new THREE.Mesh(new THREE.SphereGeometry(0.16, 32, 16), g);
  orb.position.y = 1.36; crown.add(orb);
  const crossV = new THREE.Mesh(new THREE.BoxGeometry(0.07, 0.36, 0.07), g); crossV.position.y = 1.66; crown.add(crossV);
  const crossH = new THREE.Mesh(new THREE.BoxGeometry(0.24, 0.07, 0.07), g); crossH.position.y = 1.7; crown.add(crossH);
  /* jewels around the band */
  const colors = [0xd01820, 0x1d4fd8, 0x10a050, 0x1d4fd8];
  for (let i = 0; i < 12; i++) {
    const a = (i / 12) * Math.PI * 2;
    const j = new THREE.Mesh(i % 3 === 0 ? new THREE.OctahedronGeometry(0.13, 0) : new THREE.IcosahedronGeometry(0.09, 0),
      gem(colors[i % 4]));
    j.position.set(Math.cos(a) * 1.06, 0, Math.sin(a) * 1.06);
    j.scale.z = 0.6; j.lookAt(0, 0, 0);
    crown.add(j);
  }
  scene.add(crown);

  /* light: warm key, cool rim, golden fill */
  const key = new THREE.SpotLight(0xffe2a8, 7, 0, 0.45, 0.6, 0);
  key.position.set(3, 6, 5); key.target = crown; scene.add(key);
  const rim = new THREE.DirectionalLight(0x9fb8ff, 1.2); rim.position.set(-4, 2, -5); scene.add(rim);
  scene.add(new THREE.AmbientLight(0x6a4a20, 0.25));

  /* rays of golden light behind */
  const rayTex = canvasTexture(64, 512, (c, w, h) => {
    const gr = c.createLinearGradient(0, h, 0, 0);
    gr.addColorStop(0, "rgba(255,210,120,0.9)"); gr.addColorStop(1, "rgba(255,210,120,0)");
    c.fillStyle = gr; c.fillRect(0, 0, w, h);
  });
  const rays = new THREE.Group();
  for (let i = 0; i < 22; i++) {
    const m = new THREE.Mesh(new THREE.PlaneGeometry(0.5 + Math.random() * 0.6, 12),
      new THREE.MeshBasicMaterial({ map: rayTex, transparent: true, blending: THREE.AdditiveBlending,
        depthWrite: false, opacity: 0.025 + Math.random() * 0.045, side: THREE.DoubleSide }));
    m.geometry.translate(0, 6, 0);
    m.rotation.z = (i / 22) * Math.PI * 2;
    rays.add(m);
  }
  rays.position.set(0, 2.1, -4);
  scene.add(rays);

  /* tumbling gold leaf */
  const LEAVES = 160;
  const leaves = new THREE.InstancedMesh(new THREE.PlaneGeometry(0.11, 0.15),
    new THREE.MeshStandardMaterial({ color: 0xffcf5a, metalness: 1, roughness: 0.3, side: THREE.DoubleSide }), LEAVES);
  const leaf = [];
  for (let i = 0; i < LEAVES; i++) {
    leaf.push({ x: (Math.random() - 0.5) * 12, y: 5 + Math.random() * 8, z: (Math.random() - 0.5) * 6,
      s: 0.6 + Math.random() * 0.8, r: new THREE.Euler(Math.random() * 6, Math.random() * 6, 0),
      w: new THREE.Vector3(Math.random() * 4, Math.random() * 4, Math.random() * 2), f: 0.7 + Math.random() * 0.8 });
  }
  scene.add(leaves);
  const m4 = new THREE.Matrix4(), q = new THREE.Quaternion(), one = new THREE.Vector3(1, 1, 1);

  return (t, dt) => {
    const k = ease(t / 1.6);
    crown.position.y = 1.35 + 4.5 * (1 - k) + Math.sin(t * 1.6) * 0.06;
    crown.rotation.y = t * 0.55;
    crown.rotation.x = 0.28 - 0.1 * (1 - k);
    rays.rotation.z = t * 0.06;
    for (let i = 0; i < LEAVES; i++) {
      const L = leaf[i];
      if (t > 0.8) L.y -= L.f * dt;
      L.r.x += L.w.x * dt; L.r.y += L.w.y * dt;
      q.setFromEuler(L.r);
      m4.compose(new THREE.Vector3(L.x + Math.sin(t + i) * 0.3, L.y, L.z), q, one.clone().multiplyScalar(L.s));
      leaves.setMatrixAt(i, m4);
    }
    leaves.instanceMatrix.needsUpdate = true;
  };
}

function bannerTexture() {
  const t = canvasTexture(512, 1280, (g, w, h) => {
    g.clearRect(0, 0, w, h);
    g.beginPath();                                       /* swallowtail shape */
    g.moveTo(0, 0); g.lineTo(w, 0); g.lineTo(w, h); g.lineTo(w / 2, h * 0.86); g.lineTo(0, h); g.closePath();
    const silk = g.createLinearGradient(0, 0, w, 0);
    silk.addColorStop(0, "#0b3a1e"); silk.addColorStop(0.5, "#1b6a3a"); silk.addColorStop(1, "#0b3a1e");
    g.fillStyle = silk; g.fill();
    g.save(); g.clip();
    for (let x = 0; x < w; x += 3) {                     /* the silk's weave */
      g.fillStyle = `rgba(255,255,255,${0.02 + Math.random() * 0.03})`; g.fillRect(x, 0, 1, h);
    }
    g.restore();
    g.lineWidth = 22; g.strokeStyle = "#d9a640"; g.stroke();
    g.beginPath();
    g.moveTo(44, 40); g.lineTo(w - 44, 40); g.lineTo(w - 44, h - 60); g.lineTo(w / 2, h * 0.86 - 44); g.lineTo(44, h - 60); g.closePath();
    g.setLineDash([4, 12]); g.lineWidth = 4; g.strokeStyle = "#f0cd7a"; g.stroke(); g.setLineDash([]);
    g.font = "bold 300px serif"; g.textAlign = "center"; g.textBaseline = "middle";
    g.shadowColor = "rgba(0,0,0,0.55)"; g.shadowBlur = 14; g.shadowOffsetY = 6;
    const goldText = g.createLinearGradient(0, 380, 0, 760);
    goldText.addColorStop(0, "#fff0b4"); goldText.addColorStop(0.5, "#e2ae45"); goldText.addColorStop(1, "#9c6a17");
    g.fillStyle = goldText; g.fillText("⚜", w / 2, 560);
  });
  return t;
}

function stewardScene(scene, camera) {
  scene.background = new THREE.Color(0x041009);
  scene.fog = new THREE.Fog(0x041009, 9, 22);
  camera.position.set(0, 0.2, 9); camera.lookAt(0, 0.4, 0);

  const tex = bannerTexture();
  const banners = [];
  for (const side of [-1, 1]) {
    const geo = new THREE.PlaneGeometry(2.1, 5.2, 28, 60);
    geo.translate(0, -2.6, 0);                           /* hang from the top edge */
    const mat = new THREE.MeshPhysicalMaterial({ map: tex, alphaTest: 0.5, side: THREE.DoubleSide,
      roughness: 0.55, sheen: 0.5, sheenColor: new THREE.Color(0x7fc896), sheenRoughness: 0.4, envMapIntensity: 0.5 });
    const mesh = new THREE.Mesh(geo, mat);
    mesh.position.set(side * 3.6, 3.3, 0);
    mesh.rotation.y = -side * 0.25;
    scene.add(mesh);
    banners.push({ mesh, base: Float32Array.from(geo.attributes.position.array), side });

    const rod = new THREE.Mesh(new THREE.CylinderGeometry(0.06, 0.06, 2.7, 24), gold(0.25));
    rod.rotation.z = Math.PI / 2; rod.position.set(side * 3.6, 3.32, 0.02); rod.rotation.y = -side * 0.25;
    scene.add(rod);
    for (const e of [-1, 1]) {
      const fin = new THREE.Mesh(new THREE.SphereGeometry(0.13, 24, 16), gold(0.2));
      fin.position.set(side * 3.6 + e * 1.38 * Math.cos(side * 0.25), 3.32, -e * 1.38 * Math.sin(-side * 0.25));
      scene.add(fin);
    }
  }

  const key = new THREE.SpotLight(0xfff0d0, 4, 0, 0.7, 0.5, 0);
  key.position.set(0, 7, 8); key.target.position.set(0, 0, 0); scene.add(key, key.target);
  const rim = new THREE.PointLight(0x58ff9a, 2, 0, 0); rim.position.set(0, 1, -4); scene.add(rim);
  scene.add(new THREE.AmbientLight(0x1b3a28, 0.5));

  /* dust drifting in the light */
  const N = 300, pos = new Float32Array(N * 3);
  for (let i = 0; i < N; i++) pos.set([(Math.random() - 0.5) * 14, (Math.random() - 0.5) * 8, (Math.random() - 0.5) * 6], i * 3);
  const dg = new THREE.BufferGeometry(); dg.setAttribute("position", new THREE.BufferAttribute(pos, 3));
  const dust = new THREE.Points(dg, new THREE.PointsMaterial({ size: 0.05, map: softDot("#ffe9b0"), transparent: true,
    blending: THREE.AdditiveBlending, depthWrite: false, opacity: 0.5 }));
  scene.add(dust);

  return (t) => {
    const drop = ease(t / 1.1);
    for (const b of banners) {
      b.mesh.position.y = 3.3 + (1 - drop) * 7;
      const p = b.mesh.geometry.attributes.position;
      for (let i = 0; i < p.count; i++) {
        const x = b.base[i * 3], y = b.base[i * 3 + 1];
        const hang = Math.min(1, -y / 5.2);              /* 0 at the rod, 1 at the tip */
        const z = (Math.sin(x * 1.7 + t * 2.4 + y * 1.1 + b.side) * 0.22 +
                   Math.sin(t * 1.3 - y * 2.2) * 0.1) * hang;
        p.setXYZ(i, x + Math.sin(t * 1.6 + y) * 0.04 * hang, y, z);
      }
      p.needsUpdate = true;
      b.mesh.geometry.computeVertexNormals();
    }
    dust.rotation.y = t * 0.03;
    dust.position.y = Math.sin(t * 0.4) * 0.2;
  };
}

function valarScene(scene, camera) {
  scene.background = new THREE.Color(0x02030c);
  camera.position.set(0, -0.6, 8.5); camera.lookAt(0, 0.6, 0);

  /* stars */
  const N = 2600, pos = new Float32Array(N * 3);
  for (let i = 0; i < N; i++) {
    const v = new THREE.Vector3().randomDirection().multiplyScalar(18 + Math.random() * 30);
    if (v.z > 4) v.z = -v.z;
    pos.set([v.x, v.y, v.z], i * 3);
  }
  const sg = new THREE.BufferGeometry(); sg.setAttribute("position", new THREE.BufferAttribute(pos, 3));
  const stars = new THREE.Points(sg, new THREE.PointsMaterial({ size: 0.16, map: softDot("#ffffff"), transparent: true,
    blending: THREE.AdditiveBlending, depthWrite: false, color: new THREE.Color(1.4, 1.5, 2) }));
  scene.add(stars);

  /* shafts of light pouring down */
  const shaftTex = canvasTexture(64, 512, (g, w, h) => {
    const gr = g.createLinearGradient(0, 0, 0, h);
    gr.addColorStop(0, "rgba(255,255,255,0.95)"); gr.addColorStop(0.6, "rgba(200,215,255,0.25)"); gr.addColorStop(1, "rgba(200,215,255,0)");
    g.fillStyle = gr; g.fillRect(0, 0, w, h);
    const side = g.createLinearGradient(0, 0, w, 0);
    side.addColorStop(0, "rgba(0,0,0,1)"); side.addColorStop(0.5, "rgba(0,0,0,0)"); side.addColorStop(1, "rgba(0,0,0,1)");
    g.globalCompositeOperation = "destination-out"; g.fillStyle = side; g.fillRect(0, 0, w, h);
  });
  const shafts = new THREE.Group();
  for (let i = 0; i < 14; i++) {
    const m = new THREE.Mesh(new THREE.PlaneGeometry(0.6 + Math.random() * 1.6, 16),
      new THREE.MeshBasicMaterial({ map: shaftTex, transparent: true, blending: THREE.AdditiveBlending,
        depthWrite: false, side: THREE.DoubleSide, opacity: 0.18 + Math.random() * 0.22, color: new THREE.Color(1.2, 1.25, 1.6) }));
    m.geometry.translate(0, -8, 0);
    m.position.set((Math.random() - 0.5) * 2.5, 9, (Math.random() - 0.5) * 3 - 1);
    m.rotation.z = (Math.random() - 0.5) * 0.7;
    m.rotation.y = Math.random() * Math.PI;
    shafts.add(m);
  }
  scene.add(shafts);

  /* rings of runes, tilted and turning */
  const ringMat = (c) => new THREE.MeshBasicMaterial({ map: runeTexture("#e6ecff"), transparent: true,
    blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide, color: c });
  const ring1 = new THREE.Mesh(new THREE.PlaneGeometry(7, 7), ringMat(new THREE.Color(1.7, 1.8, 2.4)));
  const ring2 = new THREE.Mesh(new THREE.PlaneGeometry(4.4, 4.4), ringMat(new THREE.Color(1.2, 1.3, 2)));
  ring1.rotation.x = -1.15; ring2.rotation.x = -1.15;
  ring1.position.y = -0.6; ring2.position.y = 1.9;
  scene.add(ring1, ring2);

  /* motes rising */
  const M = 260, mp = new Float32Array(M * 3), ms = [];
  for (let i = 0; i < M; i++) { mp.set([(Math.random() - 0.5) * 6, -4 + Math.random() * 10, (Math.random() - 0.5) * 4], i * 3); ms.push(0.4 + Math.random()); }
  const mg = new THREE.BufferGeometry(); mg.setAttribute("position", new THREE.BufferAttribute(mp, 3));
  const motes = new THREE.Points(mg, new THREE.PointsMaterial({ size: 0.07, map: softDot("#dfe8ff"), transparent: true,
    blending: THREE.AdditiveBlending, depthWrite: false, color: new THREE.Color(1.6, 1.7, 2.2) }));
  scene.add(motes);

  return (t, dt) => {
    const k = ease(t / 1.8);
    shafts.children.forEach((m, i) => { m.scale.y = k; m.material.opacity = (0.15 + 0.2 * Math.abs(Math.sin(t * 0.8 + i))) * k; });
    ring1.rotation.z = t * 0.18; ring2.rotation.z = -t * 0.3;
    ring1.scale.setScalar(0.4 + k * 0.6); ring2.scale.setScalar(0.4 + k * 0.6);
    stars.rotation.y = t * 0.01;
    camera.position.y = -0.6 + ease(t / 5) * 0.5;
    camera.lookAt(0, 0.6, 0);
    const p = mg.attributes.position;
    for (let i = 0; i < M; i++) { let y = p.getY(i) + ms[i] * dt; if (y > 6) y = -4; p.setY(i, y); }
    p.needsUpdate = true;
  };
}

/* ------------------------------------------------------------ world events
 * Each scene has an `intro` form (the full-screen announcement when the
 * event starts) and a calm form that loops behind the game while the
 * event lasts.
 */

/* a cloud with soft alpha edges, for mist and drifting cloud banks */
function mistTexture(rgb) {
  const noise = cloudTexture([255, 255, 255]).image;
  return canvasTexture(512, 256, (g, w, h) => {
    g.drawImage(noise, 0, 0, w, h);
    const img = g.getImageData(0, 0, w, h);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 4;
      const ex = Math.min(x, w - x) / (w * 0.25), ey = Math.min(y, h - y) / (h * 0.3);
      const edge = Math.min(1, ex) * Math.min(1, ey);
      img.data[i + 3] = Math.min(255, img.data[i] * 1.5 * edge * edge);
      img.data[i] = rgb[0]; img.data[i + 1] = rgb[1]; img.data[i + 2] = rgb[2];
    }
    g.putImageData(img, 0, 0);
  });
}

/* a jagged line of hills, as a flat black silhouette */
function hills(width, height, rough, seed) {
  const sh = new THREE.Shape();
  sh.moveTo(-width / 2, -height);
  let y = 0;
  for (let x = -width / 2; x <= width / 2; x += width / 90) {
    y = Math.max(0, Math.min(height, y + (Math.sin(x * 0.37 + seed) + Math.sin(x * 1.3 + seed * 2)) * rough * 0.5 +
      (Math.random() - 0.5) * rough));
    sh.lineTo(x, y);
  }
  sh.lineTo(width / 2, -height);
  return new THREE.ShapeGeometry(sh);
}

function bloodScene(scene, camera, ctx) {
  const intro = ctx.intro;
  scene.background = new THREE.Color(0x0a0204);
  scene.fog = new THREE.Fog(0x1c0507, 20, 60);
  camera.position.set(0, 0, 10);
  camera.lookAt(0, 1.2, -10);

  /* the moon: a cratered red disc that glows */
  const moonTex = canvasTexture(512, 512, (g, w) => {
    const n = cloudTexture([255, 255, 255]).image;
    g.fillStyle = "#c8c8c8"; g.fillRect(0, 0, w, w);
    g.globalAlpha = 0.5; g.drawImage(n, 0, 0, w * 2, w);
    g.globalAlpha = 1;
    for (let k = 0; k < 26; k++) {                       /* craters */
      const x = Math.random() * w, y = Math.random() * w, r = 6 + Math.random() * 34;
      const gr = g.createRadialGradient(x - r * 0.2, y - r * 0.2, r * 0.2, x, y, r);
      gr.addColorStop(0, "rgba(90,90,90,0.55)"); gr.addColorStop(0.8, "rgba(140,140,140,0.25)"); gr.addColorStop(1, "rgba(255,255,255,0)");
      g.fillStyle = gr; g.beginPath(); g.arc(x, y, r, 0, Math.PI * 2); g.fill();
    }
  });
  const moon = new THREE.Mesh(new THREE.SphereGeometry(intro ? 4.6 : 3.6, 64, 32),
    new THREE.MeshBasicMaterial({ map: moonTex, color: new THREE.Color(1.9, 0.2, 0.12), fog: false }));
  const halo = new THREE.Sprite(new THREE.SpriteMaterial({ map: softDot("#ff3020"), color: new THREE.Color(1.2, 0.25, 0.2),
    transparent: true, blending: THREE.AdditiveBlending, depthWrite: false, opacity: 0.55, fog: false }));
  halo.scale.setScalar(intro ? 30 : 24);
  const moonHome = intro ? new THREE.Vector3(0, 6.4, -30) : new THREE.Vector3(11, 6.5, -30);
  moon.position.copy(moonHome); halo.position.copy(moonHome);
  scene.add(halo, moon);

  /* cloud banks drifting across the moon */
  const mist = mistTexture([110, 18, 20]);
  const clouds = [];
  for (let k = 0; k < 6; k++) {
    const m = new THREE.Mesh(new THREE.PlaneGeometry(26, 9), new THREE.MeshBasicMaterial({ map: mist,
      transparent: true, depthWrite: false, opacity: 0.55 + Math.random() * 0.3, fog: false }));
    m.position.set((Math.random() - 0.5) * 40, 2 + Math.random() * 7, -24 + k * 0.5);
    clouds.push({ m, v: 0.4 + Math.random() * 0.6 });
    scene.add(m);
  }

  /* two ranges of hills, the far one touched by the red fog */
  const far = new THREE.Mesh(hills(90, 7, 1.2, 1), new THREE.MeshBasicMaterial({ color: 0x1a0507 }));
  far.position.set(0, -2.2, -22); scene.add(far);
  const near = new THREE.Mesh(hills(60, 5, 0.9, 4), new THREE.MeshBasicMaterial({ color: 0x030001, fog: false }));
  near.position.set(0, -4.4, -9); scene.add(near);

  /* bats */
  const wingShape = new THREE.Shape();
  wingShape.moveTo(0, 0); wingShape.lineTo(0.55, 0.22); wingShape.lineTo(0.75, 0.05);
  wingShape.lineTo(0.6, -0.04); wingShape.lineTo(0.45, 0.02); wingShape.lineTo(0.3, -0.08); wingShape.lineTo(0.12, -0.05);
  const wingGeo = new THREE.ShapeGeometry(wingShape);
  const batMat = new THREE.MeshBasicMaterial({ color: 0x050102, side: THREE.DoubleSide, fog: false });
  const bats = [];
  for (let k = 0; k < (intro ? 16 : 6); k++) {
    const b = new THREE.Group();
    const L = new THREE.Mesh(wingGeo, batMat), R = new THREE.Mesh(wingGeo, batMat);
    R.scale.x = -1;
    const body = new THREE.Mesh(new THREE.SphereGeometry(0.07, 8, 6), batMat); body.scale.set(0.8, 1.4, 0.8);
    b.add(L, R, body);
    const dir = Math.random() < 0.5 ? 1 : -1;
    b.userData = { L, R, dir, speed: 2 + Math.random() * 2.5, phase: Math.random() * 6, y: 1 + Math.random() * 6,
      z: -8 - Math.random() * 14, x: (Math.random() - 0.5) * 40, s: 0.6 + Math.random() * 0.8 };
    b.scale.setScalar(b.userData.s);
    scene.add(b); bats.push(b);
  }

  /* embers drifting up */
  const N = intro ? 260 : 140, pos = new Float32Array(N * 3), sp = [];
  for (let k = 0; k < N; k++) { pos.set([(Math.random() - 0.5) * 30, -5 + Math.random() * 14, -2 - Math.random() * 16], k * 3); sp.push(0.2 + Math.random() * 0.6); }
  const eg = new THREE.BufferGeometry(); eg.setAttribute("position", new THREE.BufferAttribute(pos, 3));
  scene.add(new THREE.Points(eg, new THREE.PointsMaterial({ size: 0.1, map: softDot("#ff6040"), transparent: true,
    blending: THREE.AdditiveBlending, depthWrite: false, color: new THREE.Color(1.6, 0.4, 0.3), fog: false })));

  return (t, dt) => {
    if (intro) {
      const k = ease(t / 2.6);
      moon.position.y = halo.position.y = -6 + (moonHome.y + 6) * k;
      if (t > 2.4 && t - dt <= 2.4) ctx.flash();
    }
    const pulse = 0.8 + 0.2 * Math.sin(t * 1.6);
    moon.material.color.setRGB(1.9 * pulse, 0.2 * pulse, 0.12 * pulse);
    halo.material.opacity = 0.45 + 0.15 * Math.sin(t * 1.6);
    moon.rotation.y = t * 0.03;
    for (const c of clouds) { c.m.position.x += c.v * dt; if (c.m.position.x > 26) c.m.position.x = -26; }
    for (const b of bats) {
      const u = b.userData;
      u.x += u.dir * u.speed * dt;
      if (u.x > 22) u.x = -22; if (u.x < -22) u.x = 22;
      b.position.set(u.x, u.y + Math.sin(t * 2 + u.phase) * 0.4, u.z);
      b.rotation.y = u.dir > 0 ? 0 : Math.PI;
      const flap = Math.sin(t * 14 + u.phase) * 0.9;
      u.L.rotation.y = flap; u.R.rotation.y = -flap;
    }
    const p = eg.attributes.position;
    for (let k = 0; k < N; k++) { let y = p.getY(k) + sp[k] * dt; if (y > 9) y = -5; p.setY(k, y); }
    p.needsUpdate = true;
  };
}

/* a gold coin: rim plus embossed faces */
function coinParts() {
  const face = canvasTexture(256, 256, (g, w) => {
    const c = w / 2;
    g.fillStyle = "#c9962e"; g.fillRect(0, 0, w, w);
    const gr = g.createRadialGradient(c * 0.7, c * 0.7, 10, c, c, c);
    gr.addColorStop(0, "#ffe9a0"); gr.addColorStop(1, "#b07a1c");
    g.fillStyle = gr; g.beginPath(); g.arc(c, c, c - 2, 0, Math.PI * 2); g.fill();
    g.strokeStyle = "#7a5212"; g.lineWidth = 8; g.beginPath(); g.arc(c, c, c - 14, 0, Math.PI * 2); g.stroke();
    for (let k = 0; k < 36; k++) {                       /* beaded edge */
      const a = (k / 36) * Math.PI * 2;
      g.fillStyle = "#fff0b8"; g.beginPath(); g.arc(c + Math.cos(a) * (c - 26), c + Math.sin(a) * (c - 26), 4, 0, Math.PI * 2); g.fill();
    }
    g.translate(c, c); g.fillStyle = "#fff4c4"; g.strokeStyle = "#7a5212"; g.lineWidth = 3;
    g.beginPath();                                       /* a sun */
    for (let k = 0; k < 24; k++) {
      const a = (k / 24) * Math.PI * 2, r = k % 2 ? 40 : 78;
      k ? g.lineTo(Math.cos(a) * r, Math.sin(a) * r) : g.moveTo(Math.cos(a) * r, Math.sin(a) * r);
    }
    g.closePath(); g.fill(); g.stroke();
    g.fillStyle = "#c9962e"; g.beginPath(); g.arc(0, 0, 30, 0, Math.PI * 2); g.fill(); g.stroke();
  });
  const rim = new THREE.MeshStandardMaterial({ color: 0xf2c14e, metalness: 1, roughness: 0.28 });
  const faces = new THREE.MeshStandardMaterial({ color: 0xffd36a, map: face, bumpMap: face, bumpScale: 3,
    metalness: 1, roughness: 0.3 });
  return { rim, faces, materials: [rim, faces, faces] };
}

function goldScene(scene, camera, ctx) {
  const intro = ctx.intro;
  scene.background = new THREE.Color(0x0e0904);
  camera.position.set(0, 0, 10); camera.lookAt(0, 0, 0);

  const key = new THREE.DirectionalLight(0xfff0c8, 2.2); key.position.set(3, 6, 6); scene.add(key);
  const glint = new THREE.PointLight(0xffe0a0, 3, 0, 0); scene.add(glint);
  scene.add(new THREE.AmbientLight(0x5a3a10, 0.5));

  /* soft golden shafts behind */
  const rayTex = canvasTexture(64, 512, (c, w, h) => {
    const gr = c.createLinearGradient(0, 0, 0, h);
    gr.addColorStop(0, "rgba(255,210,120,0.8)"); gr.addColorStop(1, "rgba(255,210,120,0)");
    c.fillStyle = gr; c.fillRect(0, 0, w, h);
    const side = c.createLinearGradient(0, 0, w, 0);     /* soft edges, no seams */
    side.addColorStop(0, "rgba(0,0,0,1)"); side.addColorStop(0.5, "rgba(0,0,0,0)"); side.addColorStop(1, "rgba(0,0,0,1)");
    c.globalCompositeOperation = "destination-out"; c.fillStyle = side; c.fillRect(0, 0, w, h);
  });
  for (let k = 0; k < 7; k++) {
    const m = new THREE.Mesh(new THREE.PlaneGeometry(1.5 + Math.random() * 2.5, 30), new THREE.MeshBasicMaterial({ map: rayTex,
      transparent: true, blending: THREE.AdditiveBlending, depthWrite: false, opacity: 0.05 + Math.random() * 0.06 }));
    m.position.set((Math.random() - 0.5) * 24, 4, -14); m.rotation.z = (Math.random() - 0.5) * 0.5;
    scene.add(m);
  }

  const parts = coinParts();
  const N = intro ? 420 : 90;
  const coins = new THREE.InstancedMesh(new THREE.CylinderGeometry(0.22, 0.22, 0.035, 32), parts.materials, N);
  const st = [];
  for (let k = 0; k < N; k++) {
    st.push({ p: new THREE.Vector3((Math.random() - 0.5) * 22, intro ? 7 + Math.random() * 14 : (Math.random() - 0.5) * 16,
      (Math.random() - 0.5) * 10 - 2), v: intro ? 3 + Math.random() * 4 : 0.5 + Math.random() * 0.8,
      r: new THREE.Euler(Math.random() * 6, Math.random() * 6, Math.random() * 6),
      w: new THREE.Vector3((Math.random() - 0.5) * 6, (Math.random() - 0.5) * 6, (Math.random() - 0.5) * 3) });
  }
  scene.add(coins);

  /* the big coin of the Gold Rush, spinning above the title */
  let big = null;
  if (intro) {
    big = new THREE.Mesh(new THREE.CylinderGeometry(1.25, 1.25, 0.16, 96), parts.materials);
    big.rotation.order = "YXZ";
    scene.add(big);
  }

  /* glitter */
  const G = 220, gp = new Float32Array(G * 3);
  for (let k = 0; k < G; k++) gp.set([(Math.random() - 0.5) * 24, (Math.random() - 0.5) * 14, (Math.random() - 0.5) * 8 - 2], k * 3);
  const gg = new THREE.BufferGeometry(); gg.setAttribute("position", new THREE.BufferAttribute(gp, 3));
  const glitter = new THREE.Points(gg, new THREE.PointsMaterial({ size: 0.06, map: softDot("#fff2c0"), transparent: true,
    blending: THREE.AdditiveBlending, depthWrite: false, color: new THREE.Color(1.1, 0.95, 0.6) }));
  scene.add(glitter);

  const m4 = new THREE.Matrix4(), q = new THREE.Quaternion(), one = new THREE.Vector3(1, 1, 1);
  return (t, dt) => {
    glint.position.set(Math.sin(t * 0.7) * 8, Math.cos(t * 0.5) * 4, 4);
    for (let k = 0; k < N; k++) {
      const c = st[k];
      c.p.y -= c.v * dt;
      if (c.p.y < -9) { c.p.y = 9; c.p.x = (Math.random() - 0.5) * 22; if (intro) c.v *= 0.5; }
      c.r.x += c.w.x * dt; c.r.y += c.w.y * dt; c.r.z += c.w.z * dt;
      q.setFromEuler(c.r); m4.compose(c.p, q, one); coins.setMatrixAt(k, m4);
    }
    coins.instanceMatrix.needsUpdate = true;
    if (big) {
      const k = ease(t / 1.4);
      big.position.set(0, 2.25 + (1 - k) * 6, 0);
      big.rotation.set(Math.PI / 2, (1 - k) * 14 + Math.sin(t * 1.3) * 0.35, 0);
    }
    glitter.material.opacity = 0.6 + 0.4 * Math.sin(t * 3);
    glitter.rotation.y = t * 0.05;
  };
}

/* a sword, point up, crossguard at the origin */
function sword() {
  const steel = new THREE.MeshStandardMaterial({ color: 0xc9d2dc, metalness: 1, roughness: 0.26, envMapIntensity: 0.35 });
  const brass = gold(0.3);
  const leather = new THREE.MeshStandardMaterial({ color: 0x3a1f12, roughness: 0.8 });
  const g = new THREE.Group();
  const sh = new THREE.Shape();
  sh.moveTo(-0.1, 0); sh.lineTo(0.1, 0); sh.lineTo(0.07, 2.35); sh.lineTo(0, 2.65); sh.lineTo(-0.07, 2.35); sh.closePath();
  const blade = new THREE.Mesh(new THREE.ExtrudeGeometry(sh, { depth: 0.02, bevelEnabled: true, bevelThickness: 0.018,
    bevelSize: 0.03, bevelSegments: 2 }), steel);
  blade.position.z = -0.01; g.add(blade);
  const guard = new THREE.Mesh(new THREE.BoxGeometry(0.8, 0.09, 0.13), brass); g.add(guard);
  for (const e of [-1, 1]) { const b = new THREE.Mesh(new THREE.SphereGeometry(0.075, 16, 12), brass); b.position.x = e * 0.42; g.add(b); }
  const grip = new THREE.Mesh(new THREE.CylinderGeometry(0.045, 0.05, 0.5, 16), leather); grip.position.y = -0.3; g.add(grip);
  const pommel = new THREE.Mesh(new THREE.SphereGeometry(0.09, 20, 14), brass); pommel.position.y = -0.6; g.add(pommel);
  return g;
}

function stoneTexture() {
  return canvasTexture(512, 512, (g, w) => {
    g.fillStyle = "#2a2430"; g.fillRect(0, 0, w, w);
    const n = cloudTexture([255, 255, 255]).image;
    for (let row = 0; row < 8; row++) for (let col = -1; col < 5; col++) {
      const x = col * 128 + (row % 2) * 64, y = row * 64;
      const l = 30 + Math.random() * 18;
      g.fillStyle = `hsl(275, 8%, ${l}%)`; g.fillRect(x + 3, y + 3, 122, 58);
    }
    g.globalAlpha = 0.35; g.globalCompositeOperation = "multiply"; g.drawImage(n, 0, 0, w, w);
  });
}

function huntScene(scene, camera, ctx) {
  const intro = ctx.intro;
  scene.background = new THREE.Color(0x07040a);
  camera.position.set(0, 0, 10); camera.lookAt(0, 0.4, 0);

  /* a castle wall lit by torches */
  const stone = stoneTexture(); stone.wrapS = stone.wrapT = THREE.RepeatWrapping; stone.repeat.set(5, 3);
  const wall = new THREE.Mesh(new THREE.PlaneGeometry(40, 22), new THREE.MeshStandardMaterial({ map: stone,
    bumpMap: stone, bumpScale: 2, roughness: 0.95, color: 0x8a7a96, envMapIntensity: 0.05 }));
  wall.position.z = -5; scene.add(wall);
  scene.add(new THREE.AmbientLight(0x3a2050, 0.45));

  const fireTex = softDot("#ffffff");
  const torches = [];
  for (const side of [-1, 1]) {
    const x = side * (intro ? 5.2 : 7.6), y = intro ? -0.6 : 0.6;
    const handle = new THREE.Mesh(new THREE.CylinderGeometry(0.07, 0.04, 1.4, 12),
      new THREE.MeshStandardMaterial({ color: 0x3b2414, roughness: 0.9 }));
    handle.position.set(x, y - 0.7, -4.4); handle.rotation.z = -side * 0.15; scene.add(handle);
    const cup = new THREE.Mesh(new THREE.CylinderGeometry(0.2, 0.1, 0.3, 16, 1, true), gold(0.45));
    cup.material.side = THREE.DoubleSide; cup.position.set(x + side * 0.1, y, -4.4); scene.add(cup);
    const light = new THREE.PointLight(0xff8a30, 22, 20, 1.2); light.position.set(x, y + 0.6, -3.8); scene.add(light);
    const flames = [];
    for (let k = 0; k < 46; k++) {
      const sp = new THREE.Sprite(new THREE.SpriteMaterial({ map: fireTex, transparent: true, blending: THREE.AdditiveBlending,
        depthWrite: false, color: new THREE.Color(1.3, 0.6, 0.2) }));
      sp.userData.life = Math.random(); scene.add(sp); flames.push(sp);
    }
    torches.push({ x: x + side * 0.1, y: y + 0.12, light, flames });
  }

  /* swords crossing above the title */
  let s1 = null, s2 = null, burst = null, burstV = [];
  if (intro) {
    s1 = sword(); s2 = sword(); scene.add(s1, s2);
    const B = 160, bp = new Float32Array(B * 3);
    for (let k = 0; k < B; k++) {
      const v = new THREE.Vector3().randomDirection().multiplyScalar(2 + Math.random() * 5); v.y = Math.abs(v.y) * 0.8 + 1;
      burstV.push(v);
    }
    const bg = new THREE.BufferGeometry(); bg.setAttribute("position", new THREE.BufferAttribute(bp, 3));
    burst = new THREE.Points(bg, new THREE.PointsMaterial({ size: 0.09, map: softDot("#ffd080"), transparent: true,
      blending: THREE.AdditiveBlending, depthWrite: false, color: new THREE.Color(3, 2, 0.8) }));
    burst.visible = false; scene.add(burst);
  }

  /* embers */
  const N = intro ? 200 : 120, pos = new Float32Array(N * 3), spd = [];
  for (let k = 0; k < N; k++) { pos.set([(Math.random() - 0.5) * 22, -6 + Math.random() * 14, -4 + Math.random() * 6], k * 3); spd.push(0.3 + Math.random() * 0.8); }
  const eg = new THREE.BufferGeometry(); eg.setAttribute("position", new THREE.BufferAttribute(pos, 3));
  scene.add(new THREE.Points(eg, new THREE.PointsMaterial({ size: 0.07, map: softDot("#ffb060"), transparent: true,
    blending: THREE.AdditiveBlending, depthWrite: false, color: new THREE.Color(2.2, 1.1, 0.4) })));

  const CROSS = 0.75;
  return (t, dt) => {
    for (const tc of torches) {
      tc.light.intensity = 20 + Math.sin(t * 17 + tc.x) * 3 + Math.sin(t * 7.3) * 4 + Math.random() * 2;
      for (const f of tc.flames) {
        const u = f.userData; u.life += dt * (1.4 + Math.random() * 0.6);
        if (u.life > 1) { u.life -= 1; u.dx = (Math.random() - 0.5) * 0.25; }
        const l = u.life;
        f.position.set(tc.x + (u.dx || 0) * (1 + l * 2) + Math.sin(t * 9 + l * 6) * 0.04, tc.y + l * 1.0, -4.3);
        f.scale.setScalar(0.55 * (1 - l) + 0.08);
        f.material.color.setRGB(1.4, 0.75 * (1 - l) + 0.2, 0.25 * (1 - l));
        f.material.opacity = (1 - l) * 0.6;
      }
    }
    if (s1) {
      const k = ease(t / CROSS);
      s1.position.set(-3.8 * (1 - k), 1.15, 0); s1.rotation.z = -0.62 - (1 - k) * 1.2;
      s2.position.set(3.8 * (1 - k), 1.15, 0); s2.rotation.z = 0.62 + (1 - k) * 1.2;
      s1.rotation.y = s2.rotation.y = Math.sin(t * 0.8) * 0.15;
      if (t >= CROSS && t - dt < CROSS) { ctx.flash(); burst.visible = true; }
      if (burst.visible) {
        const a = t - CROSS, p = burst.geometry.attributes.position;
        for (let k2 = 0; k2 < burstV.length; k2++) {
          const v = burstV[k2];
          p.setXYZ(k2, v.x * a, 2.25 + v.y * a - 4.9 * a * a, v.z * a);
        }
        p.needsUpdate = true;
        burst.material.opacity = Math.max(0, 1 - a / 1.4);
      }
    }
    const p = eg.attributes.position;
    for (let k = 0; k < N; k++) {
      let y = p.getY(k) + spd[k] * dt; if (y > 8) y = -6; p.setY(k, y);
      p.setX(k, p.getX(k) + Math.sin(t + k) * 0.003);
    }
    p.needsUpdate = true;
  };
}

/* ------------------------------------------------------------ runner */

function stage(canvas, opts) {
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: !opts.lite, powerPreference: opts.lite ? "low-power" : "high-performance" });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, opts.lite ? 1 : 1.5));
  renderer.setSize(window.innerWidth, window.innerHeight, false);
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.05;
  renderer.outputColorSpace = THREE.SRGBColorSpace;

  const scene = new THREE.Scene();
  const pmrem = new THREE.PMREMGenerator(renderer);
  scene.environment = pmrem.fromScene(new RoomEnvironment(renderer), 0.04).texture;
  const camera = new THREE.PerspectiveCamera(42, window.innerWidth / window.innerHeight, 0.1, 200);
  return { renderer, scene, camera, pmrem };
}

function run(canvas, st, update, look, opts) {
  const { renderer, scene, camera, pmrem } = st;
  const composer = new EffectComposer(renderer);
  composer.setPixelRatio(renderer.getPixelRatio() * (opts.lite ? 0.75 : 1));
  composer.setSize(window.innerWidth, window.innerHeight);
  composer.addPass(new RenderPass(scene, camera));
  if (!opts.nobloom) composer.addPass(new UnrealBloomPass(new THREE.Vector2(window.innerWidth, window.innerHeight),
    look.bloom, look.radius, look.threshold));
  composer.addPass(new OutputPass());

  const onResize = () => {
    camera.aspect = window.innerWidth / window.innerHeight; camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight, false);
    composer.setSize(window.innerWidth, window.innerHeight);
  };
  window.addEventListener("resize", onResize);

  const dispose = () => {
    alive = false;
    cancelAnimationFrame(raf);
    window.removeEventListener("resize", onResize);
    scene.traverse((o) => {
      if (o.geometry) o.geometry.dispose();
      if (o.material) [].concat(o.material).forEach((m) => {
        for (const k of ["map", "bumpMap", "emissiveMap"]) if (m[k]) m[k].dispose();
        m.dispose();
      });
    });
    if (scene.environment) scene.environment.dispose();
    pmrem.dispose();
    composer.dispose && composer.dispose();
    renderer.dispose();
    renderer.forceContextLoss();
    canvas.remove();
  };

  let raf = 0, alive = true;
  if (opts.at != null) {                  /* testing: jump to a moment */
    for (let t = 0; t < opts.at; t += 1 / 60) update(t, 1 / 60);
    composer.render();
    return { dispose };
  }

  const start = performance.now(), step = opts.fps ? 1000 / opts.fps : 0;
  let last = start, drawn = 0;
  const frame = (now) => {
    if (!alive) return;
    raf = requestAnimationFrame(frame);
    if (step && now - drawn < step - 1) return;        /* frame cap */
    if (opts.paused && opts.paused()) { last = now; return; }
    drawn = now;
    const t = (now - start) / 1000, dt = Math.min(0.05, (now - last) / 1000);
    last = now;
    update(t, dt);
    composer.render();
  };
  raf = requestAnimationFrame(frame);
  return { dispose };
}

function newCanvas(cls, host, first) {
  const canvas = document.createElement("canvas");
  canvas.className = cls;
  if (first) host.insertBefore(canvas, host.firstChild); else host.appendChild(canvas);
  return canvas;
}

function makeStage(canvas, opts) {
  try { return stage(canvas, opts); } catch (e) { canvas.remove(); throw e; }
}

const EVENT_SCENES = { 1: bloodScene, 2: goldScene, 3: huntScene };
const EVENT_LOOK = {
  1: { bloom: 0.9, threshold: 0.7, radius: 0.5 },
  2: { bloom: 0.55, threshold: 0.88, radius: 0.35 },
  3: { bloom: 0.5, threshold: 0.95, radius: 0.35 },
};

/* a ceremony or world-event announcement, full screen behind the card */
export function play(kind, host, opts = {}) {
  const canvas = newCanvas("ceremony3d", host, true);
  const st = makeStage(canvas, opts);
  const { scene, camera, renderer } = st;

  const flashEl = host.querySelector(".flash");
  const flash = () => { if (flashEl) { flashEl.style.animation = "none"; void flashEl.offsetWidth; flashEl.style.animation = ""; } };

  let update, look = { bloom: 0.9, threshold: 0.72, radius: 0.5 };
  if (kind === 101) { update = stewardScene(scene, camera); look = { bloom: 0.45, threshold: 0.85, radius: 0.5 }; }
  else if (kind === 102 || kind === 103) { update = crownScene(scene, camera, renderer); look = { bloom: 0.6, threshold: 0.8, radius: 0.5 }; }
  else if (kind === 104) { update = valarScene(scene, camera); look.bloom = 1.25; }
  else if (kind >= 201 && kind <= 203) {
    update = EVENT_SCENES[kind - 200](scene, camera, { intro: true, flash });
    look = EVENT_LOOK[kind - 200];
  }
  else if (kind >= 3) { update = wizardScene(scene, camera, kind, flash); look = { bloom: 0.85, threshold: 0.9, radius: 0.25 }; }
  else { update = apprenticeScene(scene, camera); look.bloom = 0.3; }

  return run(canvas, st, update, look, opts);
}

/* the calm scene behind the game while a world event lasts: 30 frames a
   second at a lower resolution, to be kind to batteries */
export function ambient(id, host, opts = {}) {
  const make = EVENT_SCENES[id];
  if (!make) throw new Error("no scene for event " + id);
  const canvas = newCanvas("event3d", host, false);
  const lite = Object.assign({ lite: true, fps: 30 }, opts);
  const st = makeStage(canvas, lite);
  const update = make(st.scene, st.camera, { intro: false, flash() {} });
  const look = EVENT_LOOK[id];
  return run(canvas, st, update, { bloom: look.bloom * 0.8, threshold: look.threshold, radius: look.radius }, lite);
}
