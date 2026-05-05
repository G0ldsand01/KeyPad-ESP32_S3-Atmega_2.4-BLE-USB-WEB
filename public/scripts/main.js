/**
 * FlexPad - Parallaxe, révélations au scroll, ancres douces
 * Compatible Astro View Transitions : réinit à chaque navigation (astro:page-load)
 *
 * Si le module s'exécute après DOMContentLoaded (souvent en prod / Vercel), on lance quand même l'init.
 */
function whenDocumentReady(cb) {
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', cb, { once: true });
  } else {
    queueMicrotask(cb);
  }
}

let parallaxAbort = null;
let scrollParallaxAbort = null;
let smoothScrollAbort = null;
let revealObserver = null;

/** Coefficient de défilement du fond (grille, profondeur, circuits). */
const BACKDROP_PARALLAX_FACTOR = 0.42;

function initParallax() {
  if (parallaxAbort) {
    parallaxAbort.abort();
    parallaxAbort = null;
  }

  const backdrop = document.querySelector('.main-page-backdrop');
  if (!backdrop) return;

  const parallaxLayers = backdrop.querySelectorAll('[data-speed]');
  if (parallaxLayers.length === 0) return;

  parallaxAbort = new AbortController();
  const signal = parallaxAbort.signal;

  const mainPage = document.querySelector('.main-page');

  const handleParallax = () => {
    const scrollY = window.scrollY;
    const docHeight = mainPage
      ? mainPage.offsetHeight
      : document.documentElement.scrollHeight;
    const vh = window.innerHeight;
    const maxScroll = Math.max(0, docHeight - vh);

    parallaxLayers.forEach((layer) => {
      const speed = parseFloat(layer.dataset.speed) || 0.5;
      const yPos =
        scrollY >= maxScroll
          ? -(maxScroll * speed * BACKDROP_PARALLAX_FACTOR)
          : -(scrollY * speed * BACKDROP_PARALLAX_FACTOR);
      const tilt = layer.dataset.circuitTilt;
      if (tilt != null && tilt !== '') {
        layer.style.transform = `translate3d(0, ${yPos}px, 0) rotate(${tilt}deg)`;
      } else {
        layer.style.transform = `translate3d(0, ${yPos}px, 0)`;
      }
    });
  };

  let ticking = false;
  const onScroll = () => {
    if (!ticking) {
      requestAnimationFrame(() => {
        handleParallax();
        ticking = false;
      });
      ticking = true;
    }
  };

  window.addEventListener('scroll', onScroll, { passive: true, signal });
  handleParallax();
}

/**
 * Parallaxe « au goût du jour » : léger décalage vertical selon la position dans le viewport.
 * Éviter sur les nœuds .reveal (transition transform) — utiliser un wrapper parent.
 */
function initScrollParallaxElements() {
  if (scrollParallaxAbort) {
    scrollParallaxAbort.abort();
    scrollParallaxAbort = null;
  }

  const nodes = document.querySelectorAll('[data-scroll-parallax]');
  if (nodes.length === 0) return;

  if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) return;

  scrollParallaxAbort = new AbortController();
  const { signal } = scrollParallaxAbort;

  const update = () => {
    const vh = window.innerHeight;
    const mid = vh * 0.5;
    nodes.forEach((el) => {
      const raw = el.getAttribute('data-scroll-parallax');
      const parsed = raw != null && raw !== '' ? parseFloat(raw) : 0.12;
      const intensity = Number.isFinite(parsed) ? parsed : 0.12;
      const rect = el.getBoundingClientRect();
      const elCenter = rect.top + rect.height * 0.5;
      const offset = (mid - elCenter) * intensity;
      el.style.transform = `translate3d(0, ${offset}px, 0)`;
    });
  };

  let ticking = false;
  const onScrollOrResize = () => {
    if (!ticking) {
      requestAnimationFrame(() => {
        update();
        ticking = false;
      });
      ticking = true;
    }
  };

  window.addEventListener('scroll', onScrollOrResize, { passive: true, signal });
  window.addEventListener('resize', onScrollOrResize, { passive: true, signal });
  update();
}

function initScrollReveal() {
  if (revealObserver) {
    revealObserver.disconnect();
    revealObserver = null;
  }

  const revealElements = document.querySelectorAll('.reveal');
  if (revealElements.length === 0) return;

  revealObserver = new IntersectionObserver(
    (entries) => {
      entries.forEach((entry) => {
        if (entry.isIntersecting) {
          entry.target.classList.add('visible');
        }
      });
    },
    { threshold: 0.08, rootMargin: '0px 0px -40px 0px' }
  );

  revealElements.forEach((el) => {
    revealObserver.observe(el);
    const rect = el.getBoundingClientRect();
    const vh = window.innerHeight || document.documentElement.clientHeight;
    if (rect.top < vh && rect.bottom > 0) {
      el.classList.add('visible');
    }
  });
}

function initSmoothScroll() {
  if (smoothScrollAbort) {
    smoothScrollAbort.abort();
    smoothScrollAbort = null;
  }

  smoothScrollAbort = new AbortController();
  const signal = smoothScrollAbort.signal;

  document.addEventListener(
    'click',
    (e) => {
      const link = e.target.closest?.('a[href^="#"]');
      if (!link) return;
      const href = link.getAttribute('href');
      if (!href || href === '#') return;
      const target = document.querySelector(href);
      if (target) {
        e.preventDefault();
        target.scrollIntoView({
          behavior: 'smooth',
          block: 'start',
        });
      }
    },
    { capture: true, signal }
  );
}

function initLucideIcons() {
  if (typeof lucide !== 'undefined' && lucide.createIcons) {
    lucide.createIcons();
  }
}

function initPage() {
  initParallax();
  initScrollParallaxElements();
  initScrollReveal();
  initSmoothScroll();
  initLucideIcons();
}

document.addEventListener('astro:page-load', initPage);
whenDocumentReady(initPage);
