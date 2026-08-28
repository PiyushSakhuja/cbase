/* ==========================================================================
   CBase showcase — interaction layer.

   Presentation only: no C++ engine code runs here. The visualizations model
   behavior documented in storage/*.cpp and the README:

   - Page visualizer uses the engine's real layout constants (header 6 B,
     slot 6 B, record 8 B, page 4096 B) and computes byte offsets exactly
     the way HeapFile::insert() does.
   - The buffer pool panel replays the exact policy of
     BufferPool::fetch_page(): linear hit scan, first empty frame,
     round-robin victim, dirty write-back before reuse.
   ========================================================================== */
(function () {
  'use strict';

  var $ = function (sel, root) { return (root || document).querySelector(sel); };
  var $$ = function (sel, root) {
    return Array.prototype.slice.call((root || document).querySelectorAll(sel));
  };

  /* ======================================================================
     1 · Active section highlight in the nav
     ====================================================================== */
  (function navHighlight() {
    var links = $$('#navLinks a');
    if (!links.length || !('IntersectionObserver' in window)) return;

    var map = {};
    links.forEach(function (a) {
      var id = a.getAttribute('href').slice(1);
      map[id] = a;
    });

    var observer = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
        links.forEach(function (a) { a.classList.remove('active'); });
        var link = map[entry.target.id];
        if (link) link.classList.add('active');
      });
    }, { rootMargin: '-35% 0px -60% 0px' });

    links.forEach(function (a) {
      var sec = document.getElementById(a.getAttribute('href').slice(1));
      if (sec) observer.observe(sec);
    });
  })();

  /* ======================================================================
     2 · Architecture pipeline selector
     ====================================================================== */
  (function architecture() {
    var buttons = $$('.pipe');
    var title = $('#arch-detail-title');
    var file = $('#arch-detail-file');
    var desc = $('#arch-detail-desc');
    var panel = $('#arch-detail');
    if (!buttons.length || !title || !file || !desc || !panel) return;

    var COMPONENTS = {
      cli: {
        name: 'CLI',
        file: 'main.cpp',
        desc: 'Menu-driven front end: Insert, Delete, Scan, Exit. Every read is validated and a failed stream state is recovered, so bad input can never wedge the loop.'
      },
      heap: {
        name: 'HeapFile',
        file: 'storage/heap_file.cpp',
        desc: 'Record management and RID addressing. HeapFile owns the slotted-page layout: slot directories, free-space accounting, logical deletion, and sequential scan.'
      },
      pool: {
        name: 'BufferPool',
        file: 'storage/buffer_pool.cpp',
        desc: 'Caches pages in memory and evicts frames when necessary. Three frames, round-robin replacement, and dirty write-back before a frame is reused.'
      },
      disk: {
        name: 'DiskManager',
        file: 'storage/disk_manager.cpp',
        desc: 'Handles fixed-size page I/O. Maps page ids to file offsets (page_id \u00d7 4096) and reports every failed read or write instead of ignoring it.'
      },
      file: {
        name: 'Binary File',
        file: 'mydb.dat',
        desc: 'Persistent storage: a plain file of raw 4 KB pages. Pages only arrive here through a flush or an eviction, and the state survives process restarts.'
      }
    };

    function select(key, btn) {
      var c = COMPONENTS[key];
      if (!c) return;
      buttons.forEach(function (b) {
        b.setAttribute('aria-selected', b === btn ? 'true' : 'false');
      });
      title.textContent = c.name;
      file.textContent = c.file;
      desc.textContent = c.desc;
      panel.setAttribute('aria-labelledby', btn.id);
    }

    buttons.forEach(function (btn) {
      btn.addEventListener('click', function () {
        select(btn.getAttribute('data-component'), btn);
      });
    });
  })();

  /* ======================================================================
     3 · Page visualizer — real slotted-page byte arithmetic
     ====================================================================== */
  (function pageViz() {
    var PAGE_SIZE = 4096;   // storage/config.h
    var HEADER = 6;         // sizeof(PageHeader)
    var SLOT = 6;           // sizeof(Slot)
    var RECORD = 8;         // sizeof(Record)
    var MAX_RECORDS = Math.floor((PAGE_SIZE - HEADER) / (RECORD + SLOT)); // 292
    var SHOW = 6;           // rows rendered before the ellipsis
    var INITIAL = 3;        // illustrated starting state

    var slotsEl = $('#pgSlots');
    var recordsEl = $('#pgRecords');
    var freeEl = $('#pgFree');
    var freeLabel = $('#pgFreeLabel');
    var freeBytes = $('#pgFreeBytes');
    var rdSlots = $('#rdSlots');
    var rdRecords = $('#rdRecords');
    var rdFree = $('#rdFree');
    var rdRid = $('#rdRid');
    var rdOffset = $('#rdOffset');
    var insertBtn = $('#pgInsert');
    var resetBtn = $('#pgReset');
    var note = $('#pgNote');
    if (!slotsEl || !recordsEl || !insertBtn) return;

    var n = INITIAL;

    var fmt = function (v) { return v.toLocaleString('en-US'); };

    function row(cls, label, bytes, fresh) {
      var div = document.createElement('div');
      div.className = cls + (fresh ? ' pg-new' : '');
      var l = document.createElement('span');
      l.textContent = label;
      var b = document.createElement('span');
      b.className = 'pg-bytes';
      b.textContent = 'bytes ' + bytes;
      div.appendChild(l);
      div.appendChild(b);
      return div;
    }

    function ellipsis(text) {
      var div = document.createElement('div');
      div.className = 'pg-ellipsis';
      div.textContent = text;
      return div;
    }

    function render(freshIndex) {
      // Slot directory: grows downward from the header. Slot i occupies
      // bytes [HEADER + i*SLOT, HEADER + (i+1)*SLOT).
      slotsEl.innerHTML = '';
      var shown = Math.min(n, SHOW);
      for (var i = 0; i < shown; i++) {
        slotsEl.appendChild(row('pg-slot', 'Slot ' + i + ' — 6 B',
          (HEADER + i * SLOT) + '–' + (HEADER + (i + 1) * SLOT - 1),
          i === freshIndex));
      }
      if (n > SHOW) {
        slotsEl.appendChild(ellipsis('\u00b7\u00b7\u00b7 ' + (n - SHOW) + ' more slot(s)'));
      }

      // Free space: between directory end and free_space_offset.
      var dirEnd = HEADER + n * SLOT;
      var fso = PAGE_SIZE - n * RECORD;      // HeapFile's free_space_offset
      var freeCount = fso - dirEnd;          // 4090 - 14n
      freeLabel.textContent = 'FREE SPACE — ' + fmt(freeCount) + ' B';
      freeBytes.textContent = n === 0
        ? 'bytes ' + HEADER + '–' + (PAGE_SIZE - 1)
        : 'bytes ' + dirEnd + '–' + (fso - 1);

      // Records: grow upward from the page end. Record k occupies
      // [PAGE_SIZE - (k+1)*RECORD, PAGE_SIZE - k*RECORD). Newest shown first.
      recordsEl.innerHTML = '';
      var recShown = Math.min(n, SHOW);
      for (var k = n - 1; k >= n - recShown; k--) {
        recordsEl.appendChild(row('pg-record', 'Record ' + k + ' — 8 B',
          (PAGE_SIZE - (k + 1) * RECORD) + '–' + (PAGE_SIZE - k * RECORD - 1),
          k === freshIndex));
      }
      if (n > SHOW) {
        recordsEl.appendChild(ellipsis('\u00b7\u00b7\u00b7 ' + (n - SHOW) + ' more record(s)'));
      }

      // Live readout.
      rdSlots.textContent = String(n);
      rdRecords.textContent = String(n);
      rdFree.textContent = fmt(freeCount) + ' B';
      if (n === 0) {
        rdRid.textContent = '—';
        rdOffset.textContent = '—';
      } else {
        var last = n - 1;
        rdRid.textContent = '(0, ' + last + ')';
        rdOffset.textContent = 'bytes ' +
          (PAGE_SIZE - (last + 1) * RECORD) + '–' + (PAGE_SIZE - last * RECORD - 1);
      }

      insertBtn.disabled = n >= MAX_RECORDS;
      if (n >= MAX_RECORDS) {
        note.textContent = 'Page full at ' + MAX_RECORDS +
          ' records — the engine moves on to the next page.';
      } else {
        note.textContent = 'A page holds at most ' + MAX_RECORDS +
          ' records — (4096 − 6) / (8 + 6).';
      }
    }

    insertBtn.addEventListener('click', function () {
      if (n >= MAX_RECORDS) return;
      n++;
      render(n - 1);
    });

    resetBtn.addEventListener('click', function () {
      n = INITIAL;
      render();
    });

    render();
  })();

  /* ======================================================================
     4 · Buffer pool simulation — mirrors storage/buffer_pool.cpp
     ====================================================================== */
  (function bufferPool() {
    var POOL_SIZE = 3;      // storage/config.h
    var framesEl = $('#bpFrames');
    var logEl = $('#bpLog');
    var fetchBtn = $('#bpFetch');
    var modifyBtn = $('#bpModify');
    var resetBtn = $('#bpReset');
    var chips = $$('#bpChips .chip');
    var ct = {
      hits: $('#ctHits'), misses: $('#ctMisses'),
      evictions: $('#ctEvictions'), writebacks: $('#ctWritebacks')
    };
    if (!framesEl || !logEl || !fetchBtn) return;

    var frames, nextVictim, stats, selected, logCount;

    function reset() {
      // Initial state: three pages resident after a scan, page 0 carrying
      // unflushed modifications — mirrors the example in the project brief.
      frames = [
        { page: 0, dirty: true },
        { page: 1, dirty: false },
        { page: 2, dirty: false }
      ];
      nextVictim = 0;
      stats = { hits: 0, misses: 0, evictions: 0, writebacks: 0 };
      logCount = 0;
      logEl.innerHTML = '';
      var hint = document.createElement('p');
      hint.className = 'log-hint';
      hint.textContent = 'Simulation — replays the exact policy of ' +
        'storage/buffer_pool.cpp. No engine code runs in this page.';
      logEl.appendChild(hint);
      clearFlow();
      render();
    }

    function render(flashIndex, flashClass) {
      framesEl.innerHTML = '';
      for (var i = 0; i < POOL_SIZE; i++) {
        var f = frames[i];
        var card = document.createElement('div');
        card.className = 'frame' +
          (i === flashIndex ? ' ' + flashClass : '');

        var label = document.createElement('span');
        label.className = 'frame-label mono';
        label.textContent = 'FRAME ' + i;

        var page = document.createElement('span');
        page.className = 'frame-page mono';
        page.textContent = f.page === -1 ? '—' : 'PAGE ' + f.page;

        var badge = document.createElement('span');
        badge.className = 'frame-badge mono ' +
          (f.page === -1 ? 'empty' : (f.dirty ? 'dirty' : 'clean'));
        badge.textContent = f.page === -1 ? 'EMPTY' : (f.dirty ? 'DIRTY' : 'CLEAN');

        var mark = document.createElement('span');
        mark.className = 'victim-mark mono';
        mark.textContent = (i === nextVictim && f.page !== -1)
          ? '\u2191 next eviction' : '';

        card.appendChild(label);
        card.appendChild(page);
        card.appendChild(badge);
        card.appendChild(mark);
        framesEl.appendChild(card);
      }
      ct.hits.textContent = String(stats.hits);
      ct.misses.textContent = String(stats.misses);
      ct.evictions.textContent = String(stats.evictions);
      ct.writebacks.textContent = String(stats.writebacks);
    }

    /* --- decision-flow highlighting --- */
    function clearFlow() {
      $$('#bpFlow [data-f]').forEach(function (el) {
        el.classList.remove('on', 'passed');
      });
    }
    function flowOn(key) {
      var el = $('#bpFlow [data-f="' + key + '"]');
      if (el) el.classList.add('on');
    }
    function flowPassed(key) {
      var el = $('#bpFlow [data-f="' + key + '"]');
      if (el) el.classList.add('passed');
    }

    /* --- trace log --- */
    function log(text, verdict) {
      if (logCount >= 40) logEl.removeChild(logEl.firstChild);
      logCount++;
      var p = document.createElement('p');
      if (verdict) {
        var head = document.createElement('span');
        head.className = verdict === 'HIT' ? 'hit' : 'miss';
        head.textContent = verdict + ' ';
        p.appendChild(head);
      }
      p.appendChild(document.createTextNode(text));
      logEl.appendChild(p);
      logEl.scrollTop = logEl.scrollHeight;
    }

    /* --- the policy, step for step as in BufferPool::fetch_page --- */
    function fetchPage(id) {
      clearFlow();
      flowOn('request');

      // 1. Cache hit: the page is already in a frame.
      for (var i = 0; i < POOL_SIZE; i++) {
        if (frames[i].page === id) {
          stats.hits++;
          flowOn('hit');
          log('page ' + id + ' \u2192 served from frame ' + i, 'HIT');
          render(i, 'f-hit');
          return;
        }
      }

      // 2. There is still an empty frame: load the page into it.
      flowPassed('hit');
      for (var e = 0; e < POOL_SIZE; e++) {
        if (frames[e].page === -1) {
          stats.misses++;
          frames[e] = { page: id, dirty: false };
          flowOn('empty');
          log('page ' + id + ' \u2192 loaded into empty frame ' + e, 'MISS');
          render(e, 'f-load');
          return;
        }
      }

      // 3. All frames occupied: evict one (round-robin). A dirty victim is
      //    written back to disk BEFORE its frame is reused.
      flowPassed('empty');
      flowOn('evict');
      var vi = nextVictim;
      var victim = frames[vi];
      var detail = 'page ' + id + ' \u2192 evict frame ' + vi +
        ' (page ' + victim.page + ', ' + (victim.dirty ? 'dirty' : 'clean') + ')';
      if (victim.dirty) {
        stats.writebacks++;
        flowOn('dirty');
        detail += ' \u00b7 victim written back to disk';
      } else {
        flowPassed('dirty');
      }
      stats.evictions++;
      stats.misses++;
      frames[vi] = { page: id, dirty: false };
      nextVictim = (nextVictim + 1) % POOL_SIZE;
      flowOn('load');
      detail += ' \u00b7 page ' + id + ' loaded into frame ' + vi;
      log(detail, 'MISS');
      render(vi, 'f-evict');
    }

    function modifyPage(id) {
      for (var i = 0; i < POOL_SIZE; i++) {
        if (frames[i].page === id) {
          if (frames[i].dirty) {
            log('page ' + id + ' modified \u00b7 frame ' + i +
              ' is already DIRTY');
          } else {
            frames[i].dirty = true;
            log('page ' + id + ' modified (insert/delete) \u00b7 frame ' +
              i + ' \u2192 DIRTY');
          }
          render(i, 'f-hit');
          return;
        }
      }
      log('page ' + id + ' is not cached \u00b7 fetch it first (no change)');
    }

    /* --- controls --- */
    chips.forEach(function (chip) {
      chip.addEventListener('click', function () {
        chips.forEach(function (c) {
          c.setAttribute('aria-pressed', c === chip ? 'true' : 'false');
        });
        selected = parseInt(chip.getAttribute('data-page'), 10);
      });
    });

    fetchBtn.addEventListener('click', function () {
      fetchPage(selected);
    });
    modifyBtn.addEventListener('click', function () {
      modifyPage(selected);
    });
    resetBtn.addEventListener('click', reset);

    selected = 0;
    reset();
  })();

  /* ======================================================================
     5 · Record lifecycle stepper
     ====================================================================== */
  (function lifecycle() {
    var chain = $('#lcChain');
    var countEl = $('#lcCount');
    var titleEl = $('#lcTitle');
    var descEl = $('#lcDesc');
    var nextBtn = $('#lcNext');
    var prevBtn = $('#lcPrev');
    var resetBtn = $('#lcReset');
    if (!chain || !nextBtn) return;

    var STEPS = [
      {
        t: 'INSERT',
        d: 'The CLI reads (ID, age) from the user and passes the 8-byte record to HeapFile::insert().'
      },
      {
        t: 'HeapFile',
        d: 'HeapFile selects the current page, reuses a slot freed by an earlier delete if one exists, and verifies the record still fits. The insert returns the record\u2019s RID \u2014 its permanent (page_id, slot_id) address.'
      },
      {
        t: 'Page',
        d: 'The record is written just below the previous one \u2014 records grow up from the end of the page \u2014 and a 6-byte slot (offset, size, is_used = 1) is appended to the directory growing down from the header.'
      },
      {
        t: 'BufferPool',
        d: 'The page lives in one of the pool\u2019s 3 memory frames. So far nothing has touched the disk.'
      },
      {
        t: 'Dirty Page',
        d: 'set_dirty(true) marks the frame: it now differs from the on-disk copy. This flag is the engine\u2019s guarantee that the page will eventually be written back.'
      },
      {
        t: 'Flush',
        d: 'flush_all() \u2014 or eviction of the frame \u2014 hands the page to DiskManager::write_page().'
      },
      {
        t: 'Disk',
        d: 'The 4 KB page lands at offset page_id \u00d7 4096 in the database file. Reopen the process and a scan recovers every record \u2014 this exact path is covered by the integration tests.'
      }
    ];

    var items = $$('#lcChain li');
    var current = 0;

    function render() {
      items.forEach(function (li, i) {
        li.classList.toggle('done', i < current);
        li.classList.toggle('current', i === current);
      });
      countEl.textContent = (current + 1) + ' / ' + STEPS.length;
      titleEl.textContent = STEPS[current].t;
      descEl.textContent = STEPS[current].d;
      nextBtn.disabled = current >= STEPS.length - 1;
      prevBtn.disabled = current <= 0;
    }

    nextBtn.addEventListener('click', function () {
      if (current < STEPS.length - 1) { current++; render(); }
    });
    prevBtn.addEventListener('click', function () {
      if (current > 0) { current--; render(); }
    });
    resetBtn.addEventListener('click', function () { current = 0; render(); });

    render();
  })();

  /* ======================================================================
     6 · Optional media slots
     The <img> tags ship commented out. If you drop real screenshots into
     assets/ and uncomment them, these listeners style them automatically.
     ====================================================================== */
  $$('.shot img').forEach(function (img) {
    var figure = img.closest('.shot');
    img.addEventListener('load', function () {
      figure.classList.add('is-loaded');
    });
    img.addEventListener('error', function () {
      img.remove(); // keep the dashed placeholder, never show a broken icon
    });
    if (img.complete && img.naturalWidth > 0) {
      figure.classList.add('is-loaded');
    }
  });
})();
