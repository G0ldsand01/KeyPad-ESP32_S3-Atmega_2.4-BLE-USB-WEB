/**
 * Page Contact — formulaire mailto, compatible View Transitions
 */

function whenDocumentReady(cb) {
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', cb, { once: true });
  } else {
    queueMicrotask(cb);
  }
}

let contactAbort = null;

function initContactPage() {
  if (contactAbort) {
    contactAbort.abort();
    contactAbort = null;
  }

  const form = document.getElementById('contact-form');
  if (!form) return;

  contactAbort = new AbortController();
  const { signal } = contactAbort;

  form.addEventListener(
    'submit',
    (e) => {
      e.preventDefault();
      const nameEl = document.getElementById('contact-name');
      const emailEl = document.getElementById('contact-email');
      const subjectEl = document.getElementById('contact-subject');
      const messageEl = document.getElementById('contact-message');
      const name = nameEl instanceof HTMLInputElement ? nameEl.value.trim() : '';
      const email = emailEl instanceof HTMLInputElement ? emailEl.value.trim() : '';
      const subject = subjectEl instanceof HTMLSelectElement ? subjectEl.value : '';
      const message = messageEl instanceof HTMLTextAreaElement ? messageEl.value.trim() : '';
      const body = `De : ${name}\nCourriel : ${email}\n\n${message}`;
      const mailto = `mailto:contact@flexpad.com?subject=${encodeURIComponent('[FlexPad] ' + subject)}&body=${encodeURIComponent(body)}`;
      window.location.href = mailto;
    },
    { signal }
  );
}

document.addEventListener('astro:page-load', initContactPage);
whenDocumentReady(initContactPage);
