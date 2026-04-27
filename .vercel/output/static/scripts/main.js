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
let smoothScrollAbort = null;
let revealObserver = null;

function initParallax() {
  if (parallaxAbort) {
    parallaxAbort.abort();
    parallaxAbort = null;
  }

  const backdrop = document.querySelector('.main-page-backdrop');
  if (!backdrop) return;

  parallaxAbort = new AbortController();
  const signal = parallaxAbort.signal;

  const parallaxLayers = backdrop.querySelectorAll('[data-speed]');
  if (parallaxLayers.length === 0) return;

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
          ? -(maxScroll * speed * 0.3)
          : -(scrollY * speed * 0.3);
      layer.style.transform = `translate3d(0, ${yPos}px, 0)`;
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
  initScrollReveal();
  initSmoothScroll();
  initLucideIcons();
}

document.addEventListener('astro:page-load', initPage);
whenDocumentReady(initPage);
