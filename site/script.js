/* PowerPlanner landing — interactivity */

(() => {
  // ----- Theme toggle (persisted) -----
  const root = document.documentElement;
  const toggle = document.getElementById('themeToggle');
  const stored = localStorage.getItem('pp-landing-theme');
  if (stored === 'light' || stored === 'dark') {
    root.setAttribute('data-theme', stored);
  }
  if (toggle) {
    toggle.addEventListener('click', () => {
      const next = root.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
      root.setAttribute('data-theme', next);
      localStorage.setItem('pp-landing-theme', next);
    });
  }

  // ----- Count-up animation on stat strip when visible -----
  const prefersReduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  const stats = document.querySelectorAll('.stat-num[data-target]');
  if (stats.length > 0 && 'IntersectionObserver' in window && !prefersReduced) {
    const obs = new IntersectionObserver(
      (entries) => {
        for (const entry of entries) {
          if (!entry.isIntersecting) continue;
          const el = entry.target;
          const target = Number(el.getAttribute('data-target')) || 0;
          if (el.dataset.done === '1') continue;
          el.dataset.done = '1';
          const duration = 1100;
          const start = performance.now();
          const tick = (now) => {
            const t = Math.min(1, (now - start) / duration);
            const eased = 1 - Math.pow(1 - t, 3);
            const value = Math.round(eased * target);
            el.textContent = String(value);
            if (t < 1) requestAnimationFrame(tick);
          };
          requestAnimationFrame(tick);
          obs.unobserve(el);
        }
      },
      { threshold: 0.4 },
    );
    stats.forEach((s) => obs.observe(s));
  } else {
    // Fallback: just show the targets.
    stats.forEach((s) => {
      const t = s.getAttribute('data-target');
      if (t) s.textContent = t;
    });
  }

  // ----- Subtle reveal on sections when entering viewport -----
  if ('IntersectionObserver' in window && !prefersReduced) {
    const reveal = new IntersectionObserver(
      (entries) => {
        for (const entry of entries) {
          if (entry.isIntersecting) {
            entry.target.style.opacity = '1';
            entry.target.style.transform = 'translateY(0)';
            reveal.unobserve(entry.target);
          }
        }
      },
      { threshold: 0.15 },
    );
    document.querySelectorAll('.steps, .features, .why, .shortcuts, .pricing').forEach((s) => {
      s.style.opacity = '0';
      s.style.transform = 'translateY(20px)';
      s.style.transition = 'opacity 600ms ease, transform 600ms ease';
      reveal.observe(s);
    });
  }
})();
