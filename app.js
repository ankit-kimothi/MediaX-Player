/* ==========================================================================
   MediaX — app.js
   --------------------------------------------------------------------------
   READ ME FIRST
   Plain, boring JavaScript (no build step, no framework) split into clearly
   numbered layers:

     1. DATA MODEL        -> mock media library + user playlists
     2. STATE             -> one plain object describing the whole UI
     3. DOM CACHE         -> every element the app touches, in one place
     4. HELPERS           -> formatting + tiny utilities
     5. MEDIA BRIDGE      -> dispatch() / applyCommand()  <-- BACKEND SEAM
     6. RENDER            -> "state -> DOM" functions, split by cost
     7. PLAYLIST MANAGER  -> create / rename / duplicate / delete / modify
     8. EVENTS            -> wiring of buttons, sliders, menus, modal
     9. KEYBOARD          -> global shortcuts
    10. CLOCK             -> simulated playback ticker (delete when C++ lands)
    11. BOOT              -> first render

   WHY THE RENDER LAYER IS SPLIT
   ------------------------------
   The earlier version re-rendered the whole right hand panel on every tick,
   so the list visibly flickered while a track was playing. Now:

     renderQueue()        rebuilds rows ONLY when the list itself changed
                          (different playlist, different search, add/remove).
     renderQueueActive()  just toggles CSS classes on the existing rows.
     renderProgress()     touches only the numbers and the bar widths.

   HOW THE C/C++ BACKEND PLUGS IN
   ------------------------------
   The UI never mutates state directly. Everything goes through:

       dispatch({ type: "PLAY", id: "hl-1" })

   and only `applyCommand()` is allowed to change `state`. So when the native
   engine exists you only touch `applyCommand()`:

       case "PLAY":  Native.play(id);  break;   // via WebSocket / WASM / IPC

   and you delete the simulated ticker in section 10, letting the engine push
   real position updates back through `dispatch({ type: "TICK" })`.
   ========================================================================== */

(function () {
  "use strict";

  /* ======================================================================
     1. DATA MODEL
     ====================================================================== */

  /**
   * @typedef {Object} MediaItem
   * @property {string} id        unique id
   * @property {string} title     display title
   * @property {string} artist    artist / author
   * @property {"audio"|"video"} kind
   * @property {string} format    MP3 / MP4 ...
   * @property {number} duration  seconds
   * @property {string} resolution
   * @property {string} size      human readable file size
   * @property {string} bitrate
   * @property {string} path      absolute file path on disk
   * @property {string} art       cover image url (data uri here)
   * @property {string} description
   */

  /** Inline SVG artwork keeps this prototype dependency free. */
  function cover(topColor, bottomColor) {
    var svg =
      '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 300">' +
      '<defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1">' +
      '<stop offset="0%" stop-color="' + topColor + '"/>' +
      '<stop offset="100%" stop-color="' + bottomColor + '"/>' +
      "</linearGradient></defs>" +
      '<rect width="400" height="300" fill="url(#g)"/>' +
      '<circle cx="300" cy="70" r="34" fill="rgba(255,255,255,.18)"/>' +
      '<path d="M0 230 L70 190 L130 220 L200 165 L270 205 L340 170 L400 200 L400 300 L0 300 Z" fill="rgba(0,0,0,.22)"/>' +
      '<circle cx="90" cy="120" r="10" fill="rgba(255,255,255,.12)"/>' +
      "</svg>";
    return "data:image/svg+xml;charset=utf-8," + encodeURIComponent(svg);
  }

  /** Small factory so the literal list below stays readable. */
  function makeItem(id, title, artist, kind, format, duration, extra) {
    var base = {
      id: id,
      title: title,
      artist: artist,
      kind: kind,
      format: format,
      duration: duration,
      resolution: kind === "video" ? "1920 x 1080" : "—",
      size: kind === "video" ? "1.4 GB" : "8.2 MB",
      bitrate: kind === "video" ? "4500 kbps" : "320 kbps",
      path:
        "C:\\MediaX\\" +
        (kind === "video" ? "Videos" : "Music") +
        "\\" + title + "." + format.toLowerCase(),
      art: cover("#4a5fa8", "#1d2650"),
      description: "A cinematic journey through the night, where dreams and reality collide.",
    };
    if (extra) {
      Object.keys(extra).forEach(function (key) {
        base[key] = extra[key];
      });
    }
    return base;
  }

  /* ----------------------------------------------------------------------
     HYPERLOOP PLAYLIST — EDIT THIS BLOCK TO CHANGE THE DEFAULT SONGS
     ----------------------------------------------------------------------
     Every entry below is one file that lives inside:

         ./Songs/HyperLoop/

     `file` = the file name exactly as it sits in that folder.
     `url`  = where the browser actually streams it from. While this
              prototype runs on Lovable the files are hosted on the CDN, so
              the CDN link is used. If you run the app locally, just replace
              the url with:  SONGS_DIR + entry.file   (see below) and drop the
              mp4/mp3 files into ./Songs/HyperLoop/.

     To add a song: copy one object, change file / title / artist / duration.
     To remove a song: delete its object. Order does NOT matter — the list is
     sorted alphabetically by title automatically (see sortByTitle()).
     ---------------------------------------------------------------------- */
  var SONGS_DIR = "./Songs/HyperLoop/";

  var HYPERLOOP_FILES = [
    /* EMPTY ON PURPOSE — the playlist starts blank.
       Example entry (copy, uncomment and edit):

       {
         file: "My_Song.mp4",
         url: SONGS_DIR + "My_Song.mp4",
         title: "My Song",
         artist: "Some Artist",
         duration: 180,
       },
    */
  ];


  /** Case insensitive A -> Z sort by track title. */
  function sortByTitle(list) {
    return list.slice().sort(function (a, b) {
      return a.title.toLowerCase().localeCompare(b.title.toLowerCase());
    });
  }

  /* Turn the plain file list above into real library items. */
  var HYPERLOOP_TRACKS = sortByTitle(HYPERLOOP_FILES).map(function (song, index) {
    return makeItem(
      "hl-" + (index + 1),
      song.title,
      song.artist,
      "video",              // these NCS releases ship as mp4 (video + audio)
      "MP4",
      song.duration,
      {
        objectUrl: song.url,          // what the <video> element loads
        path: SONGS_DIR + song.file,  // shown in the details panel
        art: cover("#5a4392", "#231a4d"),
        description: "NCS Hyperpop release — part of the HyperLoop playlist.",
      }
    );
  });

  /* The media library. It starts with the HyperLoop songs only; anything else
     the user imports through "Add Media" is appended at runtime. */
  var LIBRARY = HYPERLOOP_TRACKS.slice();


  /* Built-in "smart" sources. They behave like playlists but are generated
     from the library, so they cannot be renamed or deleted. */
  var SMART_SOURCES = {
    all: { name: "All Media", filter: function () { return true; } },
    audio: { name: "Audio", filter: function (m) { return m.kind === "audio"; } },
    video: { name: "Video", filter: function (m) { return m.kind === "video"; } },
    recent: { name: "Recently Played", filter: null /* handled separately */ },
  };

  /* User playlists — plain objects, exactly what the C++ side would hand us.
     All of them start EMPTY on purpose. */
  var PLAYLISTS = [
    {
      id: "pl-hyperloop",
      name: "HyperLoop",
      items: HYPERLOOP_TRACKS.map(function (track) { return track.id; }),
    },
    { id: "pl-1", name: "Favourites", items: [] },
    { id: "pl-2", name: "Study Mix", items: [] },
  ];

  var playlistSeq = PLAYLISTS.length; // used to mint new ids

  /* ======================================================================
     2. STATE
     ====================================================================== */

  var state = {
    theme: "dark",

    /* which playlist feeds the right hand panel:
       { type: "smart", id: "all" }   or   { type: "user", id: "pl-1" } */
    source: { type: "smart", id: "all" },

    search: "",          // left sidebar search
    /* (the old right hand "filter results" box was removed — the sidebar
       search is the single, global search for the whole library) */


    currentId: LIBRARY.length ? LIBRARY[0].id : null,
    isPlaying: false,
    position: 0,         // seconds
    volume: 75,
    muted: false,
    speed: 1,
    shuffle: false,
    repeat: false,

    recent: [],         // most recent first
    /* equalizer: one gain (dB) per band + the selected preset name */
    eqOn: true,
    eqPreset: "flat",
    eqGains: [0, 0, 0, 0, 0, 0],

  };


  /* Signature of the last rendered queue. This is the actual fix for the
     flickering right panel: if the signature has not changed we do not
     touch the DOM at all. */
  var lastQueueKey = "";

  /* modal mode: "create" | "rename" | "modify" | null */
  var modalMode = null;

  /* ======================================================================
     3. DOM CACHE
     ====================================================================== */

  function el(id) {
    return document.getElementById(id);
  }

  var dom = {
    // sidebar
    themeLight: el("mx-theme-light"),
    themeDark: el("mx-theme-dark"),
    search: el("mx-search"),
    nav: el("mx-nav"),
    addBtn: el("mx-add-btn"),
    fileInput: el("mx-file-input"),
    addPlaylist: el("mx-add-playlist"),
    folders: el("mx-folders"),
    eqPower: el("mx-eq-power"),
    eqPresets: el("mx-eq-presets"),
    eqBands: el("mx-eq-bands"),
    eqReset: el("mx-eq-reset"),


    // player
    player: el("mx-player"),
    stage: el("mx-stage"),
    media: el("mx-media"),
    stageArt: el("mx-stage-art"),
    stageFlash: el("mx-stage-flash"),
    stagePlay: el("mx-stage-play"),
    stageTitle: el("mx-stage-title"),
    seek: el("mx-seek"),
    seekFill: el("mx-seek-fill"),
    seekBuffer: el("mx-seek-buffer"),
    seekThumb: el("mx-seek-thumb"),
    seekTip: el("mx-seek-tip"),
    play: el("mx-play"),
    playIco: el("mx-play-ico"),
    prev: el("mx-prev"),
    next: el("mx-next"),
    back10: el("mx-back10"),
    fwd10: el("mx-fwd10"),
    mute: el("mx-mute"),
    volIco: el("mx-vol-ico"),
    volume: el("mx-volume"),
    elapsed: el("mx-elapsed"),
    total: el("mx-total"),
    shuffle: el("mx-shuffle"),
    repeat: el("mx-repeat"),
    speed: el("mx-speed"),
    fullscreen: el("mx-fullscreen"),

    // details
    detailsArt: el("mx-details-art"),
    detailsTitle: el("mx-details-title"),
    detailsArtist: el("mx-details-artist"),
    detailsDesc: el("mx-details-desc"),
    tags: el("mx-tags"),
    metaRes: el("mx-meta-res"),
    metaDuration: el("mx-meta-duration"),
    metaFormat: el("mx-meta-format"),
    metaSize: el("mx-meta-size"),
    metaPath: el("mx-meta-path"),

    // queue
    queueName: el("mx-queue-name"),
    queueCount: el("mx-queue-count"),
    queueLength: el("mx-queue-length"),
    queueMenuBtn: el("mx-queue-menu-btn"),
    queueMenu: el("mx-queue-menu"),
    
    queue: el("mx-queue"),
    queueEmpty: el("mx-queue-empty"),
    infoCurrent: el("mx-info-current"),
    infoTotal: el("mx-info-total"),
    infoSpeed: el("mx-info-speed"),
    infoBitrate: el("mx-info-bitrate"),
    eq: el("mx-eq"),

    // modal + toast
    modal: el("mx-modal"),
    modalBackdrop: el("mx-modal-backdrop"),
    modalTitle: el("mx-modal-title"),
    modalNameBody: el("mx-modal-name"),
    modalPickerBody: el("mx-modal-picker"),
    modalInput: el("mx-modal-input"),
    modalList: el("mx-modal-list"),
    modalClose: el("mx-modal-close"),
    modalCancel: el("mx-modal-cancel"),
    modalSave: el("mx-modal-save"),
    rowMenu: el("mx-row-menu"),
    toast: el("mx-toast"),
  };

  /* ======================================================================
     4. HELPERS
     ====================================================================== */

  /** 208 -> "03:28" */
  function formatTime(seconds) {
    var s = Math.max(0, Math.floor(seconds || 0));
    var m = Math.floor(s / 60);
    var r = s % 60;
    return String(m).padStart(2, "0") + ":" + String(r).padStart(2, "0");
  }

  function escapeHtml(text) {
    return String(text).replace(/[&<>"']/g, function (ch) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[ch];
    });
  }

  function findMedia(id) {
    for (var i = 0; i < LIBRARY.length; i++) {
      if (LIBRARY[i].id === id) return LIBRARY[i];
    }
    return null;
  }

  function findPlaylist(id) {
    for (var i = 0; i < PLAYLISTS.length; i++) {
      if (PLAYLISTS[i].id === id) return PLAYLISTS[i];
    }
    return null;
  }

  function currentMedia() {
    return findMedia(state.currentId);
  }

  /** Pick the item after a media row, falling back to the previous row. */
  function replacementAfterDelete(mediaId) {
    var items = visibleItems();
    var index = items.findIndex(function (media) { return media.id === mediaId; });
    if (index === -1) return null;
    return items[index + 1] || items[index - 1] || null;
  }

  /** True when a user playlist (not a smart source) is selected. */
  function activeUserPlaylist() {
    return state.source.type === "user" ? findPlaylist(state.source.id) : null;
  }

  /** Name shown in the right hand header. */
  function sourceName() {
    var pl = activeUserPlaylist();
    if (pl) return pl.name;
    return (SMART_SOURCES[state.source.id] || SMART_SOURCES.all).name;
  }

  /** The media items behind the current source, before searching. */
  function sourceItems() {
    var pl = activeUserPlaylist();
    if (pl) return pl.items.map(findMedia).filter(Boolean);

    if (state.source.id === "recent") {
      return state.recent.map(findMedia).filter(Boolean);
    }

    var smart = SMART_SOURCES[state.source.id] || SMART_SOURCES.all;
    return LIBRARY.filter(smart.filter);
  }

  /**
   * Items shown in the right panel.
   *
   * The left search is deliberately GLOBAL: it starts from the complete
   * library even when a user playlist is selected. The smaller right search
   * then filters whichever result set is currently on screen.
   */
  function visibleItems() {
    var globalNeedle = state.search.trim().toLowerCase();
    var localNeedle = "";
    var items = globalNeedle ? LIBRARY.slice() : sourceItems();

    if (globalNeedle) {
      items = items.filter(function (m) {
        return searchableText(m).indexOf(globalNeedle) !== -1;
      });
    }

    if (!localNeedle) return items;
    return items.filter(function (m) {
      return searchableText(m).indexOf(localNeedle) !== -1;
    });
  }

  function searchableText(media) {
    return (
      media.title + " " + media.artist + " " + media.format + " " + media.path
    ).toLowerCase();
  }

  var toastTimer = null;

  function toast(message) {
    dom.toast.textContent = message;
    dom.toast.hidden = false;
    // restart the entry animation on every message
    dom.toast.style.animation = "none";
    void dom.toast.offsetWidth;
    dom.toast.style.animation = "";
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () {
      dom.toast.hidden = true;
    }, 2200);
  }

  /* ======================================================================
     5. MEDIA BRIDGE   <-- THE ONLY PLACE THAT MUTATES `state`
     ----------------------------------------------------------------------
     dispatch()      = "please do this"      (called from the UI)
     applyCommand()  = "this is what it does" (swap for native calls later)
     ====================================================================== */

  function dispatch(command) {
    applyCommand(command);
    syncMediaElement(command.type);
    renderAfter(command.type);
  }

  function applyCommand(cmd) {
    switch (cmd.type) {
      /* ---------- transport ---------- */
      case "PLAY":
        if (cmd.id && cmd.id !== state.currentId) {
          state.currentId = cmd.id;
          state.position = 0;
          rememberRecent(cmd.id);
        }
        state.isPlaying = true;
        break;

      case "PAUSE":
        state.isPlaying = false;
        break;

      case "TOGGLE":
        state.isPlaying = !state.isPlaying;
        if (state.isPlaying) rememberRecent(state.currentId);
        break;

      case "STOP":
        state.isPlaying = false;
        state.position = 0;
        break;

      case "SEEK":
        state.position = clampPosition(cmd.seconds);
        break;

      case "SKIP":
        state.position = clampPosition(state.position + cmd.seconds);
        break;

      case "NEXT":
        state.currentId = neighbourId(1);
        state.position = 0;
        rememberRecent(state.currentId);
        break;

      case "PREV":
        // like every real player: restart first, jump back only if near start
        if (state.position > 4) {
          state.position = 0;
        } else {
          state.currentId = neighbourId(-1);
          state.position = 0;
          rememberRecent(state.currentId);
        }
        break;

      case "TICK":
        if (!state.isPlaying) break;
        state.position += cmd.delta * state.speed;
        if (state.position >= mediaDuration()) {
          if (state.repeat) {
            state.position = 0;
          } else {
            applyCommand({ type: "NEXT" });
          }
        }
        break;

      /* ---------- audio ---------- */
      case "SET_VOLUME":
        state.volume = Math.min(100, Math.max(0, cmd.value));
        state.muted = state.volume === 0;
        break;

      case "TOGGLE_MUTE":
        state.muted = !state.muted;
        break;

      case "SET_SPEED":
        state.speed = cmd.value;
        break;

      case "TOGGLE_SHUFFLE":
        state.shuffle = !state.shuffle;
        break;

      case "TOGGLE_REPEAT":
        state.repeat = !state.repeat;
        break;

      /* ---------- library / playlists ---------- */
      case "SELECT_SOURCE":
        state.source = { type: cmd.sourceType, id: cmd.id };
        break;

      case "REORDER_PLAYLIST":
        /* drag & drop inside a user playlist: move one id to a new index */
        var ordered = findPlaylist(cmd.playlistId);
        if (ordered) {
          var oldIndex = ordered.items.indexOf(cmd.mediaId);
          if (oldIndex !== -1) {
            ordered.items.splice(oldIndex, 1);
            var target = Math.max(0, Math.min(ordered.items.length, cmd.toIndex));
            ordered.items.splice(target, 0, cmd.mediaId);
          }
        }
        break;

      case "SET_SEARCH":
        state.search = cmd.value;
        break;

      case "SET_EQ_PRESET":
        state.eqPreset = cmd.name;
        if (EQ_PRESETS[cmd.name]) state.eqGains = EQ_PRESETS[cmd.name].slice();
        break;

      case "SET_EQ_BAND":
        state.eqGains[cmd.index] = cmd.value;
        state.eqPreset = "custom";
        break;

      case "TOGGLE_EQ":
        state.eqOn = !state.eqOn;
        break;


      case "CREATE_PLAYLIST":
        playlistSeq += 1;
        PLAYLISTS.push({ id: "pl-" + playlistSeq, name: cmd.name, items: [] });
        state.source = { type: "user", id: "pl-" + playlistSeq };
        break;

      case "RENAME_PLAYLIST":
        var renamed = findPlaylist(cmd.id);
        if (renamed) renamed.name = cmd.name;
        break;

      case "DUPLICATE_PLAYLIST":
        var origin = findPlaylist(cmd.id);
        if (origin) {
          playlistSeq += 1;
          PLAYLISTS.push({
            id: "pl-" + playlistSeq,
            name: origin.name + " copy",
            items: origin.items.slice(),
          });
          state.source = { type: "user", id: "pl-" + playlistSeq };
        }
        break;

      case "DELETE_PLAYLIST":
        PLAYLISTS = PLAYLISTS.filter(function (p) {
          return p.id !== cmd.id;
        });
        state.source = { type: "smart", id: "all" };
        break;

      /* Imported files (today from <input type="file">, tomorrow from C++). */
      case "ADD_MEDIA":
        cmd.items.forEach(function (item) {
          if (!findMedia(item.id)) LIBRARY.push(item);
        });
        // when a user playlist is open, drop the imports straight into it
        var openPl = activeUserPlaylist();
        if (openPl) {
          cmd.items.forEach(function (item) {
            if (openPl.items.indexOf(item.id) === -1) openPl.items.push(item.id);
          });
        }
        if (cmd.items.length) {
          state.currentId = cmd.items[0].id;
          state.position = 0;
        }
        break;

      case "ADD_TO_PLAYLIST":
        var dest = findPlaylist(cmd.playlistId);
        if (dest && dest.items.indexOf(cmd.mediaId) === -1) {
          dest.items.push(cmd.mediaId);
        }
        break;

      /* Move = add to the destination, remove from the source playlist. */
      case "MOVE_TO_PLAYLIST":
        applyCommand({ type: "ADD_TO_PLAYLIST", playlistId: cmd.toId, mediaId: cmd.mediaId });
        applyCommand({ type: "REMOVE_FROM_PLAYLIST", playlistId: cmd.fromId, mediaId: cmd.mediaId });
        break;

      case "SET_PLAYLIST_ITEMS":
        var target = findPlaylist(cmd.id);
        if (target) target.items = cmd.items.slice();
        break;

      case "REMOVE_FROM_PLAYLIST":
        var from = findPlaylist(cmd.playlistId);
        if (from) {
          from.items = from.items.filter(function (id) {
            return id !== cmd.mediaId;
          });
        }
        break;

      case "DELETE_MEDIA": {
        /* The queue X is a real delete, not merely a playlist unlink. Remove
           every reference so All Media, playlists and saved sessions cannot
           resurrect a ghost or duplicate entry after the next launch. */
        var doomed = findMedia(cmd.mediaId);
        if (!doomed) break;
        var wasCurrent = state.currentId === cmd.mediaId;
        var keepPlaying = wasCurrent && state.isPlaying;
        var replacement = wasCurrent ? replacementAfterDelete(cmd.mediaId) : null;

        if (doomed.objectUrl && doomed.objectUrl.indexOf("blob:") === 0) {
          URL.revokeObjectURL(doomed.objectUrl);
        }
        if (doomed.storageKey) deleteStoredMedia(doomed.storageKey);

        LIBRARY = LIBRARY.filter(function (media) { return media.id !== cmd.mediaId; });
        PLAYLISTS.forEach(function (playlist) {
          playlist.items = playlist.items.filter(function (id) { return id !== cmd.mediaId; });
        });
        state.recent = state.recent.filter(function (id) { return id !== cmd.mediaId; });

        if (wasCurrent) {
          state.currentId = replacement ? replacement.id : null;
          state.position = 0;
          state.isPlaying = !!(replacement && keepPlaying);
          if (replacement) rememberRecent(replacement.id);
        }
        break;
      }

      /* ---------- misc ---------- */
      case "SET_THEME":
        state.theme = cmd.value;
        document.documentElement.setAttribute("data-theme", cmd.value);
        break;


      default:
        // Unknown commands are ignored on purpose: the native backend may
        // send events this prototype does not know about yet.
        break;
    }
  }

  function mediaDuration() {
    var m = currentMedia();
    return m ? m.duration : 0;
  }

  /* ----------------------------------------------------------------------
     Browser playback adapter
     ----------------------------------------------------------------------
     Imported files carry a temporary object URL. This adapter translates the
     same UI commands intended for the future C++ engine into HTMLMediaElement
     operations today. Demo rows have no object URL and keep using the visual
     simulation, so the prototype remains useful before files are imported.
     ---------------------------------------------------------------------- */

  /* ----------------------------------------------------------------------
     5b. EQUALIZER ENGINE
     Six BiquadFilter nodes chained between the <video> element and the
     speakers. The graph is created lazily on the first user gesture, because
     browsers refuse to start an AudioContext before that. When the native
     C/C++ engine takes over, only applyEqToGraph() has to be re-pointed —
     the state (state.eqGains) and the UI stay exactly the same.
     ---------------------------------------------------------------------- */

  var EQ_BANDS = [60, 230, 910, 3000, 6000, 14000]; // Hz, low -> high
  var EQ_PRESETS = {
    flat:   [0, 0, 0, 0, 0, 0],
    bass:   [7, 5, 1, 0, -1, -2],
    treble: [-3, -2, 0, 2, 5, 7],
    vocal:  [-3, 0, 4, 5, 2, -1],
    rock:   [5, 2, -1, 2, 4, 5],
    pop:    [-1, 2, 5, 4, 1, -2],
    custom: [0, 0, 0, 0, 0, 0],
  };

  var audioCtx = null;
  var eqFilters = [];

  /** Build the audio graph once; safe to call as often as we like. */
  function ensureEqGraph() {
    if (audioCtx) return true;
    var Ctor = window.AudioContext || window.webkitAudioContext;
    if (!Ctor) return false;

    try {
      audioCtx = new Ctor();
      var node = audioCtx.createMediaElementSource(dom.media);
      eqFilters = EQ_BANDS.map(function (freq, index) {
        var filter = audioCtx.createBiquadFilter();
        filter.type = index === 0 ? "lowshelf"
          : index === EQ_BANDS.length - 1 ? "highshelf"
          : "peaking";
        filter.frequency.value = freq;
        filter.Q.value = 1;
        filter.gain.value = 0;
        node.connect(filter);
        node = filter;
        return filter;
      });
      node.connect(audioCtx.destination);
      return true;
    } catch (err) {
      audioCtx = null;
      eqFilters = [];
      return false;
    }
  }

  /** Push state.eqGains onto the filter nodes (no-op before the graph exists). */
  function applyEqToGraph() {
    if (!eqFilters.length) return;
    eqFilters.forEach(function (filter, index) {
      filter.gain.value = state.eqOn ? state.eqGains[index] || 0 : 0;
    });
  }

  /** Render the six vertical sliders once at boot. */
  function buildEqBands() {
    dom.eqBands.innerHTML = EQ_BANDS.map(function (freq, index) {
      var label = freq >= 1000 ? freq / 1000 + "k" : String(freq);
      return (
        '<div class="eq-band">' +
          '<span class="eq-gain">0 dB</span>' +
          '<input type="range" min="-12" max="12" step="1" value="0" ' +
            'data-band="' + index + '" orient="vertical" ' +
            'aria-label="' + label + ' hertz gain" />' +
          '<span class="eq-hz">' + label + "</span>" +
        "</div>"
      );
    }).join("");
  }

  var loadedMediaId = null;


  function hasBrowserSource(media) {
    return !!(media && media.objectUrl);
  }

  function loadCurrentMedia() {
    var media = currentMedia();
    var playable = hasBrowserSource(media);

    dom.media.classList.toggle("is-visible", !!(playable && media.kind === "video"));
    dom.stageArt.style.opacity = playable && media.kind === "video" ? "0" : "1";

    if (!playable) {
      if (loadedMediaId !== null) {
        dom.media.pause();
        dom.media.removeAttribute("src");
        dom.media.load();
      }
      loadedMediaId = null;
      return false;
    }

    if (loadedMediaId !== media.id) {
      loadedMediaId = media.id;
      dom.media.src = media.objectUrl;
      dom.media.load();
    }
    return true;
  }

  function requestBrowserPlay() {
    // first real playback is also our first user gesture -> safe to build EQ
    if (ensureEqGraph()) {
      if (audioCtx.state === "suspended") audioCtx.resume();
      applyEqToGraph();
    }
    var promise = dom.media.play();
    if (promise && typeof promise.catch === "function") {
      promise.catch(function () {
        state.isPlaying = false;
        renderTransport();
        toast("Playback could not start. Try another supported media file.");
      });
    }
  }

  function syncMediaElement(commandType) {
    var isPlayable = loadCurrentMedia();
    if (!isPlayable) return;

    dom.media.volume = (state.muted ? 0 : state.volume) / 100;
    dom.media.playbackRate = state.speed;

    if (commandType === "SEEK" || commandType === "SKIP" || commandType === "STOP") {
      if (Number.isFinite(dom.media.duration)) dom.media.currentTime = state.position;
    }

    if (
      commandType === "PLAY" || commandType === "TOGGLE" ||
      commandType === "NEXT" || commandType === "PREV" || commandType === "DELETE_MEDIA"
    ) {
      if (state.isPlaying) requestBrowserPlay();
      else dom.media.pause();
    } else if (commandType === "PAUSE" || commandType === "STOP") {
      dom.media.pause();
    }
  }

  function clampPosition(seconds) {
    return Math.min(mediaDuration(), Math.max(0, seconds));
  }

  function rememberRecent(id) {
    state.recent = [id].concat(
      state.recent.filter(function (x) {
        return x !== id;
      })
    ).slice(0, 12);
  }

  /** Next / previous id inside the currently visible queue. */
  function neighbourId(step) {
    var items = visibleItems();
    if (!items.length) return state.currentId;

    if (state.shuffle && items.length > 1) {
      var pick;
      do {
        pick = items[Math.floor(Math.random() * items.length)];
      } while (pick.id === state.currentId);
      return pick.id;
    }

    var index = -1;
    for (var i = 0; i < items.length; i++) {
      if (items[i].id === state.currentId) index = i;
    }
    var nextIndex = (index + step + items.length) % items.length;
    return items[nextIndex].id;
  }

  /* ======================================================================
     6. RENDER
     Split by cost so playback never rebuilds lists (no more flicker).
     ====================================================================== */

  /** Cheap: only the things that change while the clock runs. */
  function renderProgress() {
    var media = currentMedia();
    var duration = media ? media.duration : 0;
    var percent = duration ? (state.position / duration) * 100 : 0;

    dom.seekFill.style.width = percent + "%";
    dom.seekThumb.style.left = percent + "%";
    dom.seekBuffer.style.width = Math.min(100, percent + 12) + "%";
    dom.seek.setAttribute("aria-valuenow", Math.round(percent));

    dom.elapsed.textContent = formatTime(state.position);
    dom.total.textContent = formatTime(duration);
    dom.infoCurrent.textContent = formatTime(state.position);
    dom.infoTotal.textContent = formatTime(duration);
  }

  /** Play/pause icons, toggle states, animated decorations. */
  function renderTransport() {
    var playing = state.isPlaying;

    dom.playIco.innerHTML = playing
      ? '<path d="M8 5h3v14H8zM13 5h3v14h-3z" class="fill" />'
      : '<path d="M8 5l12 7-12 7z" class="fill" />';
    dom.play.setAttribute("aria-label", playing ? "Pause (Space)" : "Play (Space)");

    dom.stage.classList.toggle("is-playing", playing);
    dom.eq.classList.toggle("is-playing", playing);

    dom.shuffle.classList.toggle("is-on", state.shuffle);
    dom.repeat.classList.toggle("is-on", state.repeat);

    var silent = state.muted || state.volume === 0;
    dom.volIco.innerHTML = silent
      ? '<path d="M5 9v6h4l5 4V5L9 9z" class="fill" /><path d="M17 9l4 6M21 9l-4 6" />'
      : '<path d="M5 9v6h4l5 4V5L9 9z" class="fill" /><path d="M17 9.5a3.5 3.5 0 010 5" />';
    dom.volume.value = silent ? 0 : state.volume;

    dom.speed.value = String(state.speed);
    dom.infoSpeed.textContent = state.speed.toFixed(2).replace(/0$/, "") + "x";
  }

  /** Everything describing the current media item. */
  function renderNowPlaying() {
    var media = currentMedia();
    if (!media) {
      dom.stageArt.removeAttribute("src");
      dom.stageArt.alt = "";
      dom.stageTitle.textContent = "No songs found";
      dom.detailsArt.removeAttribute("src");
      dom.detailsArt.alt = "";
      dom.detailsArt.hidden = true;
      dom.detailsTitle.textContent = "No songs found";
      dom.detailsArtist.textContent = "Add media to start listening";
      dom.detailsDesc.textContent = "Your media library is empty.";
      dom.tags.innerHTML = "";
      dom.metaRes.textContent = "—";
      dom.metaDuration.textContent = "—";
      dom.metaFormat.textContent = "—";
      dom.metaSize.textContent = "—";
      dom.metaPath.textContent = "—";
      dom.metaPath.title = "";
      dom.infoBitrate.textContent = "—";
      loadCurrentMedia();
      return;
    }

    dom.stageArt.src = media.art;
    dom.stageArt.alt = media.title + " artwork";
    dom.stageTitle.textContent = media.title + " · " + media.artist;

    dom.detailsArt.src = media.art;
    dom.detailsArt.alt = media.title + " cover";
    dom.detailsArt.hidden = false;
    dom.detailsTitle.textContent = media.title;
    dom.detailsArtist.textContent = media.artist;
    dom.detailsDesc.textContent = media.description;

    dom.tags.innerHTML =
      '<li class="tag accent">' + escapeHtml(media.kind.toUpperCase()) + "</li>" +
      '<li class="tag">' + escapeHtml(media.format) + "</li>" +
      '<li class="tag">' + escapeHtml(media.bitrate) + "</li>" +
      '<li class="tag">' + formatTime(media.duration) + "</li>";

    dom.metaRes.textContent = media.resolution;
    dom.metaDuration.textContent = formatTime(media.duration);
    dom.metaFormat.textContent = media.format;
    dom.metaSize.textContent = media.size;
    dom.metaPath.textContent = media.path;
    dom.metaPath.title = media.path;
    dom.infoBitrate.textContent = media.bitrate;

    loadCurrentMedia();
  }

  /** Left column: playlist rows + active nav item. */
  function renderPlaylists() {
    dom.folders.innerHTML = PLAYLISTS.map(function (pl, index) {
      var active = state.source.type === "user" && state.source.id === pl.id;
      return (
        '<li>' +
        '<button class="folder' + (active ? " is-active" : "") + '"' +
        ' data-playlist="' + pl.id + '" style="animation-delay:' + index * 35 + 'ms">' +
        '<span class="dot"></span>' +
        '<span class="folder-name">' + escapeHtml(pl.name) + "</span>" +
        '<span class="folder-count">' + pl.items.length + "</span>" +
        "</button></li>"
      );
    }).join("");

    Array.prototype.forEach.call(dom.nav.children, function (btn) {
      var isActive =
        state.source.type === "smart" && btn.dataset.source === state.source.id;
      btn.classList.toggle("is-active", isActive);
    });
  }

  /** Right column header (playlist name + totals). */
  function renderQueueHeader() {
    var name = state.search.trim() ? "Search Results" : sourceName();
    if (dom.queueName.textContent !== name) {
      dom.queueName.textContent = name;
      // tiny cross fade so the swap is noticeable
      dom.queueName.classList.remove("is-swapping");
      void dom.queueName.offsetWidth;
      dom.queueName.classList.add("is-swapping");
    }

    var items = visibleItems();
    var seconds = items.reduce(function (sum, m) {
      return sum + m.duration;
    }, 0);

    dom.queueCount.textContent = items.length;
    dom.queueLength.textContent = Math.round(seconds / 60) + " min";
  }

  /**
   * Expensive: rebuilds the rows. Guarded by a signature so it only runs
   * when the list content really changed.
   */
  function renderQueue(force) {
    var items = visibleItems();
    var key =
      state.source.type + ":" + state.source.id + "|" +
      state.search + "|" +
      items.map(function (m) { return m.id; }).join(",");

    if (!force && key === lastQueueKey) {
      renderQueueActive();
      return;
    }
    lastQueueKey = key;

    var draggable = state.source.type === "user";

    dom.queue.innerHTML = items.map(function (m, index) {
      var isCurrent = m.id === state.currentId;
      return (
        '<li class="q-item' + (isCurrent ? " is-current" : "") +
        (isCurrent && state.isPlaying ? " is-playing" : "") +
        (draggable ? " is-draggable" : "") + '"' +
        (draggable ? ' draggable="true"' : "") +
        ' data-id="' + m.id + '" style="animation-delay:' + Math.min(index * 28, 320) + 'ms">' +
          '<span class="q-thumb">' +
            '<img src="' + m.art + '" alt="" />' +
            '<span class="q-bars"><i></i><i></i><i></i></span>' +
          "</span>" +
          '<span class="q-body">' +
            '<span class="q-title">' + escapeHtml(m.title) + "</span>" +
            '<span class="q-artist">' + escapeHtml(m.artist) + " · " + escapeHtml(m.format) + "</span>" +
          "</span>" +
          '<span class="q-right">' +
            '<span class="q-dur tnum">' + formatTime(m.duration) + "</span>" +
            '<button class="q-add" data-add="' + m.id + '" aria-label="Add or move to a playlist" title="Add / move to playlist">' +
              '<svg viewBox="0 0 24 24" class="ico"><path d="M4 7h11M4 12h8M4 17h8" /><path d="M17 11v8M13 15h8" /></svg></button>' +
            '<button class="q-remove" data-remove="' + m.id + '" aria-label="Delete ' + escapeHtml(m.title) + ' from MediaX" title="Delete from MediaX">' +
              '<svg viewBox="0 0 24 24" class="ico"><path d="M4 7h16M9 7V5h6v2M7 7l1 13h8l1-13" /></svg></button>' +
          "</span>" +
        "</li>"
      );
    }).join("");

    dom.queueEmpty.hidden = items.length !== 0;
    dom.queueEmpty.textContent =
      state.search
        ? "No media matches your search."
        : "No songs found. Use Add Media to import a file.";
  }

  /** Cheap: only moves the highlight between existing rows. */
  function renderQueueActive() {
    Array.prototype.forEach.call(dom.queue.children, function (row) {
      var isCurrent = row.dataset.id === state.currentId;
      row.classList.toggle("is-current", isCurrent);
      row.classList.toggle("is-playing", isCurrent && state.isPlaying);
    });
  }

  /** Equalizer card: chips, slider values, bypass state. */
  function renderEq() {
    Array.prototype.forEach.call(dom.eqPresets.children, function (chip) {
      chip.classList.toggle("is-active", chip.dataset.preset === state.eqPreset);
    });

    Array.prototype.forEach.call(dom.eqBands.children, function (band, index) {
      var input = band.querySelector("input");
      var value = state.eqGains[index] || 0;
      if (Number(input.value) !== value) input.value = value;
      band.querySelector(".eq-gain").textContent =
        (value > 0 ? "+" : "") + value.toFixed(0) + " dB";
    });

    dom.eqBands.classList.toggle("is-off", !state.eqOn);
    dom.eqPower.classList.toggle("is-on", state.eqOn);
    dom.eqPower.setAttribute("aria-pressed", String(state.eqOn));

    applyEqToGraph();
  }


  /** Theme buttons. */
  function renderTheme() {
    dom.themeDark.classList.toggle("is-active", state.theme === "dark");
    dom.themeLight.classList.toggle("is-active", state.theme === "light");
  }

  /** Full paint — used on boot and after structural commands. */
  function renderAll() {
    renderTheme();
    renderPlaylists();
    renderQueueHeader();
    renderQueue(true);
    renderNowPlaying();
    renderTransport();
    renderProgress();
    renderEq();
  }

  /**
   * Decide how much work a command needs. Keeping this table in one place
   * is what stops the panel from flickering 4 times per second.
   */
  function renderAfter(type) {
    switch (type) {
      case "TICK":
        renderProgress();
        return;

      case "SEEK":
      case "SKIP":
        renderProgress();
        return;

      case "SET_VOLUME":
      case "TOGGLE_MUTE":
      case "SET_SPEED":
      case "TOGGLE_SHUFFLE":
      case "TOGGLE_REPEAT":
        renderTransport();
        return;

      case "PLAY":
      case "PAUSE":
      case "TOGGLE":
      case "STOP":
      case "NEXT":
      case "PREV":
        renderNowPlaying();
        renderTransport();
        renderProgress();
        renderQueueActive();
        // "Recently Played" is the only list that changes while playing
        if (state.source.type === "smart" && state.source.id === "recent") {
          renderQueue(false);
          renderQueueHeader();
        }
        return;

      case "SET_THEME":
        renderTheme();
        return;

      case "SET_EQ_PRESET":
      case "SET_EQ_BAND":
      case "TOGGLE_EQ":
        renderEq();
        return;


      default:
        renderAll();
    }
  }

  /* ======================================================================
     7. PLAYLIST MANAGER (create / rename / duplicate / delete / modify)
     All of it lives in the ⋯ menu of the right hand card, plus the small
     "+" button in the sidebar.
     ====================================================================== */

  function openModal(mode) {
    modalMode = mode;
    var pl = activeUserPlaylist();

    dom.modalNameBody.hidden = mode === "modify";
    dom.modalPickerBody.hidden = mode !== "modify";

    if (mode === "create") {
      dom.modalTitle.textContent = "New playlist";
      dom.modalInput.value = "";
      dom.modalInput.placeholder = "e.g. Late night mix";
    } else if (mode === "rename") {
      dom.modalTitle.textContent = "Rename playlist";
      dom.modalInput.value = pl ? pl.name : "";
    } else if (mode === "modify") {
      dom.modalTitle.textContent = "Modify " + (pl ? pl.name : "playlist");
      dom.modalList.innerHTML = LIBRARY.map(function (m, index) {
        var checked = pl && pl.items.indexOf(m.id) !== -1;
        return (
          '<li><label class="pick" style="animation-delay:' + Math.min(index * 20, 200) + 'ms">' +
          '<input type="checkbox" value="' + m.id + '"' + (checked ? " checked" : "") + " />" +
          '<span class="pick-title">' + escapeHtml(m.title) + " — " + escapeHtml(m.artist) + "</span>" +
          '<span class="pick-kind">' + escapeHtml(m.format) + "</span>" +
          "</label></li>"
        );
      }).join("");
    }

    dom.modal.hidden = false;
    if (mode !== "modify") {
      dom.modalInput.focus();
      dom.modalInput.select();
    }
  }

  function closeModal() {
    dom.modal.hidden = true;
    modalMode = null;
  }

  function saveModal() {
    var pl = activeUserPlaylist();

    if (modalMode === "create") {
      var name = dom.modalInput.value.trim() || "Untitled playlist";
      dispatch({ type: "CREATE_PLAYLIST", name: name });
      toast('Playlist "' + name + '" created');
    } else if (modalMode === "rename" && pl) {
      var newName = dom.modalInput.value.trim();
      if (newName) {
        dispatch({ type: "RENAME_PLAYLIST", id: pl.id, name: newName });
        toast("Playlist renamed");
      }
    } else if (modalMode === "modify" && pl) {
      var picked = Array.prototype.slice
        .call(dom.modalList.querySelectorAll("input:checked"))
        .map(function (input) {
          return input.value;
        });
      dispatch({ type: "SET_PLAYLIST_ITEMS", id: pl.id, items: picked });
      toast(picked.length + " tracks in " + pl.name);
    }

    closeModal();
  }

  function toggleQueueMenu(open) {
    var willOpen = open === undefined ? dom.queueMenu.hidden : open;
    dom.queueMenu.hidden = !willOpen;
    dom.queueMenuBtn.setAttribute("aria-expanded", String(willOpen));

    if (willOpen) {
      // smart sources cannot be edited — grey those entries out
      var isUser = !!activeUserPlaylist();
      Array.prototype.forEach.call(dom.queueMenu.querySelectorAll(".menu-item"), function (item) {
        item.disabled = !isUser;
      });
    }
  }

  /* ======================================================================
     8. EVENTS
     ====================================================================== */

  /* ---- 8.1 theme ---- */
  dom.themeLight.addEventListener("click", function () {
    dispatch({ type: "SET_THEME", value: "light" });
  });
  dom.themeDark.addEventListener("click", function () {
    dispatch({ type: "SET_THEME", value: "dark" });
  });

  /* ---- 8.2 searches ---- */
  dom.search.addEventListener("input", function (event) {
    dispatch({ type: "SET_SEARCH", value: event.target.value });
  });

  /* ---- 8.3 library navigation + playlists ---- */
  dom.nav.addEventListener("click", function (event) {
    var btn = event.target.closest(".nav-item");
    if (!btn) return;
    dispatch({ type: "SELECT_SOURCE", sourceType: "smart", id: btn.dataset.source });
  });

  dom.folders.addEventListener("click", function (event) {
    var btn = event.target.closest(".folder");
    if (!btn) return;
    dispatch({ type: "SELECT_SOURCE", sourceType: "user", id: btn.dataset.playlist });
  });

  dom.addPlaylist.addEventListener("click", function () {
    openModal("create");
  });

  /* ---- 8.4 add media (the real file list arrives from C++ later) ---- */
  dom.addBtn.addEventListener("click", async function () {
    /* Chromium's File System Access API lets MediaX remember the user's
       chosen file handle. Other browsers use the hidden input and keep a
       private IndexedDB copy instead. Both routes survive a browser restart. */
    if (typeof window.showOpenFilePicker === "function") {
      try {
        var handles = await window.showOpenFilePicker({
          multiple: true,
          types: [{
            description: "Audio and video files",
            accept: {
              "audio/*": [".mp3", ".wav", ".ogg", ".m4a", ".aac", ".flac"],
              "video/*": [".mp4", ".webm", ".mov", ".mkv", ".avi"],
            },
          }],
        });
        var files = await Promise.all(handles.map(function (handle) { return handle.getFile(); }));
        await importMediaFiles(files, handles);
      } catch (err) {
        if (!err || err.name !== "AbortError") toast("Media access was not granted.");
      }
      return;
    }
    dom.fileInput.click();
  });

  /**
   * Turns a browser File into the same shape as the mock library items, so
   * the rest of the UI does not care where the media came from. The C++
   * backend will hand us the very same object over the bridge.
   */
  function fileFingerprint(file) {
    return [file.name.toLowerCase(), file.size, file.lastModified || 0, file.type || ""].join("|");
  }

  function stableImportId(fingerprint) {
    var hash = 2166136261;
    for (var i = 0; i < fingerprint.length; i++) {
      hash ^= fingerprint.charCodeAt(i);
      hash = Math.imul(hash, 16777619);
    }
    return "imp-" + (hash >>> 0).toString(36);
  }

  function findImportedFile(file, fingerprint) {
    var format = (file.name.split(".").pop() || "FILE").toUpperCase();
    var title = file.name.replace(/\.[^.]+$/, "");
    var sizeText = file.size >= 1024 * 1024
      ? (file.size / (1024 * 1024)).toFixed(1) + " MB"
      : Math.round(file.size / 1024) + " KB";
    return LIBRARY.find(function (media) {
      return media.storageKey === fingerprint ||
        (media.path === file.name && media.title === title && media.format === format && media.size === sizeText);
    }) || null;
  }

  /* ------------------------------------------------------------------
     ARTWORK EXPOSURE
     Covers are small UI previews, where naturally dark frames become nearly
     black. This shadow lift raises visibility without bleaching highlights or
     changing hue. ARTWORK_VERSION forces old cached dark covers to regenerate.
     ------------------------------------------------------------------ */

  var ARTWORK_VERSION = 2;

  /** Average perceived luminance (0..1) of a canvas. */
  function canvasLuminance(context, width, height) {
    var data = context.getImageData(0, 0, width, height).data;
    var total = 0;
    var samples = 0;
    for (var i = 0; i < data.length; i += 4 * 16) { // every 16th pixel is plenty
      total += (0.2126 * data[i] + 0.7152 * data[i + 1] + 0.0722 * data[i + 2]) / 255;
      samples++;
    }
    return samples ? total / samples : 0;
  }

  /** Lift shadows with gamma and a restrained gain while retaining colour. */
  function exposeCanvas(context, width, height) {
    var luminance = canvasLuminance(context, width, height);
    if (luminance <= 0.001 || luminance >= 0.5) return;
    var image = context.getImageData(0, 0, width, height);
    var pixels = image.data;
    var gain = Math.min(1.65, Math.max(1.08, 0.46 / luminance));
    var gamma = luminance < 0.2 ? 0.68 : 0.78;
    for (var i = 0; i < pixels.length; i += 4) {
      pixels[i] = Math.min(255, 255 * Math.pow(pixels[i] / 255, gamma) * gain);
      pixels[i + 1] = Math.min(255, 255 * Math.pow(pixels[i + 1] / 255, gamma) * gain);
      pixels[i + 2] = Math.min(255, 255 * Math.pow(pixels[i + 2] / 255, gamma) * gain);
    }
    context.putImageData(image, 0, 0);
  }

  /** Extract the best looking video frame for queue/detail artwork. */
  function videoCoverFromFile(file) {
    return new Promise(function (resolve) {
      var video = document.createElement("video");
      var url = URL.createObjectURL(file);
      var canvas = document.createElement("canvas");
      canvas.width = 480;
      canvas.height = 270;
      var context = canvas.getContext("2d", { willReadFrequently: true });
      // fractions of the clip we try, in order
      var samplePoints = [0.12, 0.32, 0.55, 0.75];
      var pointIndex = 0;
      var best = { luminance: 0, data: null };
      var finished = false;

      function done(result) {
        if (finished) return;
        finished = true;
        URL.revokeObjectURL(url);
        video.removeAttribute("src");
        resolve(result || null);
      }

      function finish() {
        if (!best.data) return done(null);
        var preview = new Image();
        preview.onload = function () {
          context.clearRect(0, 0, canvas.width, canvas.height);
          context.drawImage(preview, 0, 0, canvas.width, canvas.height);
          exposeCanvas(context, canvas.width, canvas.height);
          done(canvas.toDataURL("image/jpeg", 0.88));
        };
        preview.onerror = function () { done(best.data); };
        preview.src = best.data;
      }

      function seekNext() {
        if (!context || pointIndex >= samplePoints.length) return finish();
        var duration = Number.isFinite(video.duration) && video.duration > 0 ? video.duration : 1;
        var target = duration * samplePoints[pointIndex++];
        try { video.currentTime = Math.min(Math.max(target, 0.05), Math.max(duration - 0.1, 0.05)); }
        catch (err) { finish(); }
      }

      video.muted = true;
      video.preload = "auto";

      video.addEventListener("loadeddata", seekNext, { once: true });

      video.addEventListener("seeked", function () {
        if (finished || !context) return;
        try {
          context.drawImage(video, 0, 0, canvas.width, canvas.height);
          var luminance = canvasLuminance(context, canvas.width, canvas.height);
          if (luminance > best.luminance) {
            best = { luminance: luminance, data: canvas.toDataURL("image/jpeg", 0.82) };
          }
          // a clearly bright frame is good enough, stop early
          if (luminance >= 0.34) return finish();
        } catch (err) { return finish(); }
        seekNext();
      });

      video.addEventListener("error", function () { done(null); }, { once: true });
      setTimeout(finish, 8000);
      video.src = url;
    });
  }


  /** Read ID3 APIC artwork from an MP3 without loading the entire file. */
  async function audioCoverFromFile(file) {
    if (!/\.mp3$/i.test(file.name)) return null;
    try {
      var bytes = new Uint8Array(await file.slice(0, 2 * 1024 * 1024).arrayBuffer());
      if (String.fromCharCode(bytes[0], bytes[1], bytes[2]) !== "ID3") return null;
      for (var i = 10; i + 10 < bytes.length; i++) {
        if (String.fromCharCode(bytes[i], bytes[i + 1], bytes[i + 2], bytes[i + 3]) !== "APIC") continue;
        var size = (bytes[i + 4] << 24) | (bytes[i + 5] << 16) | (bytes[i + 6] << 8) | bytes[i + 7];
        var start = i + 10;
        var encoding = bytes[start++];
        var mimeEnd = start;
        while (mimeEnd < bytes.length && bytes[mimeEnd] !== 0) mimeEnd++;
        var mime = new TextDecoder("latin1").decode(bytes.slice(start, mimeEnd)) || "image/jpeg";
        start = mimeEnd + 2; // separator + picture type
        var separatorSize = encoding === 1 || encoding === 2 ? 2 : 1;
        while (start + separatorSize < bytes.length) {
          if (bytes[start] === 0 && (separatorSize === 1 || bytes[start + 1] === 0)) break;
          start += separatorSize;
        }
        start += separatorSize;
        var end = Math.min(bytes.length, i + 10 + size);
        if (end > start) {
          return await new Promise(function (resolve) {
            var reader = new FileReader();
            reader.onload = function () { resolve(reader.result); };
            reader.onerror = function () { resolve(null); };
            reader.readAsDataURL(new Blob([bytes.slice(start, end)], { type: mime }));
          });
        }
      }
    } catch (err) {}
    return null;
  }

  /** Apply the same readable shadow lift to embedded audio cover artwork. */
  function exposeArtworkDataUrl(source) {
    return new Promise(function (resolve) {
      if (!source || source.indexOf("data:image/") !== 0) return resolve(source);
      var image = new Image();
      image.onload = function () {
        var canvas = document.createElement("canvas");
        canvas.width = 480;
        canvas.height = 480;
        var context = canvas.getContext("2d", { willReadFrequently: true });
        if (!context) return resolve(source);
        context.drawImage(image, 0, 0, canvas.width, canvas.height);
        exposeCanvas(context, canvas.width, canvas.height);
        resolve(canvas.toDataURL("image/jpeg", 0.9));
      };
      image.onerror = function () { resolve(source); };
      image.src = source;
    });
  }

  async function artworkFromFile(file, isVideo) {
    var artwork = await (isVideo ? videoCoverFromFile(file) : audioCoverFromFile(file));
    if (!artwork) return cover("#4a5fa8", "#1d2650");
    return isVideo ? artwork : exposeArtworkDataUrl(artwork);
  }

  /* ------------------------------------------------------------------
     BITRATE ANALYSIS
     A container's average bitrate is simply the number of bits divided by
     the playing time:   bitrate = (bytes * 8) / duration(seconds)
     We probe the duration with a throwaway media element (metadata only,
     no decoding of the whole file) and format the result in kbps / Mbps.
     The future C/C++ engine can replace this with real per-stream values.
     ------------------------------------------------------------------ */

  /** Load only the metadata of a file to learn its duration (seconds). */
  function probeDuration(file, isVideo) {
    return new Promise(function (resolve) {
      var element = document.createElement(isVideo ? "video" : "audio");
      var url = URL.createObjectURL(file);
      var settled = false;
      function done(value) {
        if (settled) return;
        settled = true;
        URL.revokeObjectURL(url);
        element.removeAttribute("src");
        resolve(Number.isFinite(value) && value > 0 ? value : 0);
      }
      element.preload = "metadata";
      element.muted = true;
      element.addEventListener("loadedmetadata", function () { done(element.duration); }, { once: true });
      element.addEventListener("error", function () { done(0); }, { once: true });
      setTimeout(function () { done(0); }, 5000);
      element.src = url;
    });
  }

  /** Human readable average bitrate, or "unknown" when we cannot tell. */
  function formatBitrate(byteSize, durationSeconds) {
    if (!byteSize || !durationSeconds || durationSeconds <= 0) return "unknown";
    var kbps = (byteSize * 8) / durationSeconds / 1000;
    if (!Number.isFinite(kbps) || kbps <= 0) return "unknown";
    return kbps >= 1000
      ? (kbps / 1000).toFixed(2) + " Mbps"
      : Math.round(kbps) + " kbps";
  }

  /** Recalculate the bitrate of an item once a reliable duration is known. */
  function refreshBitrate(media) {
    if (!media || !media.byteSize) return false;
    var next = formatBitrate(media.byteSize, media.duration);
    if (next === "unknown" || next === media.bitrate) return false;
    media.bitrate = next;
    return true;
  }

  async function mediaFromFile(file, index, handle) {
    var isVideo = /^video\//.test(file.type) || /\.(mp4|mkv|avi|mov|webm)$/i.test(file.name);
    var format = (file.name.split(".").pop() || "FILE").toUpperCase();
    var megabytes = file.size / (1024 * 1024);
    var fingerprint = fileFingerprint(file);
    var duplicate = findImportedFile(file, fingerprint);
    var objectUrl = URL.createObjectURL(file);

    if (duplicate) {
      if (duplicate.objectUrl && duplicate.objectUrl.indexOf("blob:") === 0) URL.revokeObjectURL(duplicate.objectUrl);
      duplicate.objectUrl = objectUrl;
      duplicate.storageKey = fingerprint;
      duplicate.byteSize = file.size;
      if (!duplicate.art || duplicate.art.indexOf("data:image/svg+xml") === 0) {
        duplicate.art = await artworkFromFile(file, isVideo);
      }
      duplicate.artworkVersion = ARTWORK_VERSION;
      if (!duplicate.bitrate || duplicate.bitrate === "unknown") {
        var knownDuration = duplicate.duration || (await probeDuration(file, isVideo));
        duplicate.duration = knownDuration || duplicate.duration;
        duplicate.bitrate = formatBitrate(file.size, duplicate.duration);
      }
      await storeMediaFile(fingerprint, file, handle);
      return duplicate;
    }

    // duration first — the bitrate maths and the queue row both need it
    var duration = await probeDuration(file, isVideo);

    var item = {
      id: stableImportId(fingerprint),
      title: file.name.replace(/\.[^.]+$/, ""),
      artist: "Local file",
      kind: isVideo ? "video" : "audio",
      format: format,
      duration: duration || 180,
      resolution: isVideo ? "unknown" : "—",
      size: megabytes >= 1 ? megabytes.toFixed(1) + " MB" : Math.round(file.size / 1024) + " KB",
      byteSize: file.size,                       // raw bytes, used by the bitrate maths
      bitrate: formatBitrate(file.size, duration),
      path: file.name,
      art: await artworkFromFile(file, isVideo),
      artworkVersion: ARTWORK_VERSION,
      description: "Imported from your device. Metadata will be filled in by the media engine.",
      objectUrl: objectUrl,
      storageKey: fingerprint,
    };
    await storeMediaFile(fingerprint, file, handle);
    return item;
  }


  async function importMediaFiles(files, handles) {
    if (!files.length) return;
    var items = await Promise.all(files.map(function (file, index) {
      return mediaFromFile(file, index, handles && handles[index]);
    }));
    var openPl = activeUserPlaylist();
    dispatch({ type: "ADD_MEDIA", items: items });
    toast(
      items.length + (items.length === 1 ? " file added" : " files added") +
        (openPl ? " to " + openPl.name : " to your library")
    );
  }

  dom.fileInput.addEventListener("change", async function (event) {
    var files = Array.prototype.slice.call(event.target.files || []);
    if (!files.length) return;
    await importMediaFiles(files, null);
    event.target.value = "";
  });

  /* Keep UI state synchronized with the real media element. These events are
     authoritative for imported files; the interval below only drives demos. */
  dom.media.addEventListener("loadedmetadata", function () {
    var media = currentMedia();
    if (!media || media.id !== loadedMediaId) return;

    if (Number.isFinite(dom.media.duration)) media.duration = dom.media.duration;
    if (media.kind === "video" && dom.media.videoWidth && dom.media.videoHeight) {
      media.resolution = dom.media.videoWidth + " x " + dom.media.videoHeight;
    }
    // now that the true duration is known, the average bitrate is computable
    refreshBitrate(media);

    state.position = dom.media.currentTime || 0;
    renderNowPlaying();
    renderProgress();
    renderQueueHeader();
    renderQueue(true);
    saveSession();
  });

  dom.media.addEventListener("timeupdate", function () {
    if (!hasBrowserSource(currentMedia()) || scrubbing) return;
    state.position = dom.media.currentTime;
    renderProgress();
  });

  dom.media.addEventListener("play", function () {
    state.isPlaying = true;
    renderTransport();
    renderQueueActive();
  });

  dom.media.addEventListener("pause", function () {
    state.isPlaying = false;
    renderTransport();
    renderQueueActive();
  });

  dom.media.addEventListener("ended", function () {
    if (state.repeat) {
      dom.media.currentTime = 0;
      requestBrowserPlay();
      return;
    }
    dispatch({ type: "NEXT" });
  });

  dom.media.addEventListener("error", function () {
    state.isPlaying = false;
    renderTransport();
    toast("This browser cannot decode that media format.");
  });

  /* ---- 8.5 equalizer card ---- */
  dom.eqPresets.addEventListener("click", function (event) {
    var chip = event.target.closest(".eq-chip");
    if (!chip) return;
    dispatch({ type: "SET_EQ_PRESET", name: chip.dataset.preset });
    toast("Equalizer: " + chip.textContent);
  });

  dom.eqBands.addEventListener("input", function (event) {
    var input = event.target.closest("input[type=range]");
    if (!input) return;
    dispatch({ type: "SET_EQ_BAND", index: Number(input.dataset.band), value: Number(input.value) });
  });

  dom.eqPower.addEventListener("click", function () {
    dispatch({ type: "TOGGLE_EQ" });
    toast(state.eqOn ? "Equalizer on" : "Equalizer bypassed");
  });

  /* ---- 8.5b drag & drop reordering (user playlists only) ----
     HTML5 drag events keep this dependency free. While a row is dragged we
     mark the row under the pointer with .is-drop-before / .is-drop-after so
     the user sees exactly where the track will land. */
  var dragId = null;

  function clearDropHints() {
    Array.prototype.forEach.call(dom.queue.children, function (row) {
      row.classList.remove("is-drop-before", "is-drop-after");
    });
  }

  dom.queue.addEventListener("dragstart", function (event) {
    var row = event.target.closest(".q-item.is-draggable");
    if (!row) return;
    dragId = row.dataset.id;
    row.classList.add("is-dragging");
    event.dataTransfer.effectAllowed = "move";
    try { event.dataTransfer.setData("text/plain", dragId); } catch (err) {}
  });

  dom.queue.addEventListener("dragover", function (event) {
    if (!dragId) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = "move";
    var row = event.target.closest(".q-item");
    clearDropHints();
    if (!row || row.dataset.id === dragId) return;
    var box = row.getBoundingClientRect();
    var after = event.clientY > box.top + box.height / 2;
    row.classList.add(after ? "is-drop-after" : "is-drop-before");
  });

  dom.queue.addEventListener("drop", function (event) {
    if (!dragId) return;
    event.preventDefault();

    var playlist = activeUserPlaylist();
    var row = event.target.closest(".q-item");
    clearDropHints();
    if (!playlist || !row || row.dataset.id === dragId) return;

    var box = row.getBoundingClientRect();
    var after = event.clientY > box.top + box.height / 2;
    var target = playlist.items.indexOf(row.dataset.id);
    var from = playlist.items.indexOf(dragId);
    if (target === -1 || from === -1) return;

    var to = target + (after ? 1 : 0);
    if (from < to) to -= 1;

    dispatch({ type: "REORDER_PLAYLIST", playlistId: playlist.id, mediaId: dragId, toIndex: to });
    renderQueue(true);
  });

  dom.queue.addEventListener("dragend", function () {
    dragId = null;
    clearDropHints();
    Array.prototype.forEach.call(dom.queue.children, function (row) {
      row.classList.remove("is-dragging");
    });
  });

  dom.eqReset.addEventListener("click", function () {
    dispatch({ type: "SET_EQ_PRESET", name: "flat" });
    toast("Bands reset");
  });

  /* ---- 8.6 the stage itself: click anywhere = play / pause ---- */
  dom.stage.addEventListener("click", function (event) {
    // clicks that land on the overlay chrome must not toggle playback
    if (event.target.closest(".chrome")) return;
    dispatch({ type: "TOGGLE" });
    flashStage();
  });

  dom.stage.addEventListener("keydown", function (event) {
    if (event.key === "Enter") {
      dispatch({ type: "TOGGLE" });
      flashStage();
    }
  });

  dom.stage.addEventListener("dblclick", function (event) {
    if (event.target.closest(".chrome")) return;
    toggleFullscreen();
  });

  /** Small ripple + icon burst in the middle of the stage. */
  function flashStage() {
    dom.stageFlash.innerHTML = state.isPlaying
      ? '<svg viewBox="0 0 24 24" class="ico"><path d="M8 5l12 7-12 7z" class="fill" /></svg>'
      : '<svg viewBox="0 0 24 24" class="ico"><path d="M8 5h3v14H8zM13 5h3v14h-3z" class="fill" /></svg>';
    dom.stageFlash.classList.remove("is-on");
    void dom.stageFlash.offsetWidth;
    dom.stageFlash.classList.add("is-on");
  }

  dom.stagePlay.addEventListener("click", function (event) {
    event.stopPropagation();
    dispatch({ type: "TOGGLE" });
  });

  /* ---- 8.7 auto hiding chrome (YouTube behaviour) ---- */
  var idleTimer = null;

  function wakeChrome() {
    dom.player.classList.remove("is-idle");
    clearTimeout(idleTimer);
    idleTimer = setTimeout(function () {
      if (state.isPlaying) dom.player.classList.add("is-idle");
    }, 2600);
  }

  dom.player.addEventListener("mousemove", wakeChrome);
  dom.player.addEventListener("mouseleave", function () {
    clearTimeout(idleTimer);
    if (state.isPlaying) dom.player.classList.add("is-idle");
  });
  dom.player.addEventListener("focusin", wakeChrome);

  /* ---- 8.8 transport buttons ---- */
  dom.play.addEventListener("click", function () {
    dispatch({ type: "TOGGLE" });
  });
  dom.next.addEventListener("click", function () {
    dispatch({ type: "NEXT" });
  });
  dom.prev.addEventListener("click", function () {
    dispatch({ type: "PREV" });
  });
  dom.back10.addEventListener("click", function () {
    dispatch({ type: "SKIP", seconds: -10 });
  });
  dom.fwd10.addEventListener("click", function () {
    dispatch({ type: "SKIP", seconds: 10 });
  });
  dom.shuffle.addEventListener("click", function () {
    dispatch({ type: "TOGGLE_SHUFFLE" });
    toast(state.shuffle ? "Shuffle on" : "Shuffle off");
  });
  dom.repeat.addEventListener("click", function () {
    dispatch({ type: "TOGGLE_REPEAT" });
    toast(state.repeat ? "Repeat on" : "Repeat off");
  });
  dom.mute.addEventListener("click", function () {
    dispatch({ type: "TOGGLE_MUTE" });
  });
  dom.volume.addEventListener("input", function (event) {
    dispatch({ type: "SET_VOLUME", value: Number(event.target.value) });
  });
  dom.speed.addEventListener("change", function (event) {
    dispatch({ type: "SET_SPEED", value: Number(event.target.value) });
  });
  dom.fullscreen.addEventListener("click", toggleFullscreen);

  function toggleFullscreen() {
    if (document.fullscreenElement) {
      document.exitFullscreen();
    } else if (dom.player.requestFullscreen) {
      dom.player.requestFullscreen();
    }
  }

  /* ---- 8.9 seek bar: click, drag and hover tooltip ---- */
  function ratioFromEvent(event) {
    var box = dom.seek.getBoundingClientRect();
    return Math.min(1, Math.max(0, (event.clientX - box.left) / box.width));
  }

  var scrubbing = false;

  dom.seek.addEventListener("pointerdown", function (event) {
    scrubbing = true;
    dom.seek.classList.add("is-scrubbing");
    dom.seek.setPointerCapture(event.pointerId);
    dispatch({ type: "SEEK", seconds: ratioFromEvent(event) * mediaDuration() });
  });

  dom.seek.addEventListener("pointermove", function (event) {
    var ratio = ratioFromEvent(event);
    var box = dom.seek.getBoundingClientRect();

    dom.seekTip.hidden = false;
    dom.seekTip.textContent = formatTime(ratio * mediaDuration());
    dom.seekTip.style.left = ratio * box.width + "px";

    if (scrubbing) {
      dispatch({ type: "SEEK", seconds: ratio * mediaDuration() });
    }
  });

  dom.seek.addEventListener("pointerup", function () {
    scrubbing = false;
    dom.seek.classList.remove("is-scrubbing");
  });

  dom.seek.addEventListener("pointerleave", function () {
    dom.seekTip.hidden = true;
  });

  dom.seek.addEventListener("keydown", function (event) {
    if (event.key === "ArrowRight") dispatch({ type: "SKIP", seconds: 5 });
    if (event.key === "ArrowLeft") dispatch({ type: "SKIP", seconds: -5 });
  });

  /* ---- 8.10 queue rows ---- */
  dom.queue.addEventListener("click", function (event) {
    var add = event.target.closest(".q-add");
    if (add) {
      event.stopPropagation();
      openRowMenu(add, add.dataset.add);
      return;
    }

    var remove = event.target.closest(".q-remove");
    if (remove) {
      event.stopPropagation();
      var media = findMedia(remove.dataset.remove);
      if (media && window.confirm('Delete "' + media.title + '" from MediaX and every playlist?')) {
        dispatch({ type: "DELETE_MEDIA", mediaId: media.id });
        toast("Deleted from MediaX");
      }
      return;
    }

    var row = event.target.closest(".q-item");
    if (!row) return;
    dispatch({ type: "PLAY", id: row.dataset.id });
    toast("Playing " + findMedia(row.dataset.id).title);
  });

  /* ---- 8.10b per row "add / move to playlist" popover ---- */

  /**
   * Builds and positions the little popover next to the clicked "+" button.
   *  - inside a smart source  -> "Add to" list only
   *  - inside a user playlist -> "Move to" list (add + remove in one go)
   */
  function openRowMenu(anchor, mediaId) {
    var from = activeUserPlaylist();
    var media = findMedia(mediaId);
    if (!media) return;

    var rows = PLAYLISTS.filter(function (pl) {
      return !from || pl.id !== from.id;
    });

    var html =
      '<p class="row-menu-head">' +
      (from ? "Move &ldquo;" : "Add &ldquo;") + escapeHtml(media.title) + "&rdquo; to" +
      "</p>";

    if (!rows.length) {
      html += '<p class="row-menu-empty">No other playlist yet.</p>';
    } else {
      html += rows.map(function (pl) {
        var already = pl.items.indexOf(mediaId) !== -1;
        return (
          '<button class="row-menu-item" role="menuitem"' +
          ' data-target="' + pl.id + '"' + (already && !from ? " disabled" : "") + ">" +
          '<span class="dot"></span>' +
          '<span class="row-menu-name">' + escapeHtml(pl.name) + "</span>" +
          '<span class="row-menu-count">' + (already ? "added" : pl.items.length) + "</span>" +
          "</button>"
        );
      }).join("");
    }

    if (from) {
      html +=
        '<div class="menu-sep" role="separator"></div>' +
        '<button class="row-menu-item danger" role="menuitem" data-target="__remove">' +
        '<span class="row-menu-name">Remove from ' + escapeHtml(from.name) + "</span></button>";
    }

    dom.rowMenu.innerHTML = html;
    dom.rowMenu.hidden = false;
    dom.rowMenu.dataset.media = mediaId;

    // keep the popover inside the viewport
    var box = anchor.getBoundingClientRect();
    var width = dom.rowMenu.offsetWidth;
    var height = dom.rowMenu.offsetHeight;
    var left = Math.max(8, Math.min(box.left - width + box.width, window.innerWidth - width - 8));
    var top = box.bottom + 6;
    if (top + height > window.innerHeight - 8) top = Math.max(8, box.top - height - 6);

    dom.rowMenu.style.left = left + "px";
    dom.rowMenu.style.top = top + "px";
  }

  function closeRowMenu() {
    dom.rowMenu.hidden = true;
  }

  dom.rowMenu.addEventListener("click", function (event) {
    var item = event.target.closest(".row-menu-item");
    if (!item || item.disabled) return;

    var mediaId = dom.rowMenu.dataset.media;
    var from = activeUserPlaylist();
    closeRowMenu();

    if (item.dataset.target === "__remove") {
      dispatch({ type: "REMOVE_FROM_PLAYLIST", playlistId: from.id, mediaId: mediaId });
      toast("Removed from " + from.name);
      return;
    }

    var dest = findPlaylist(item.dataset.target);
    if (!dest) return;

    if (from) {
      dispatch({ type: "MOVE_TO_PLAYLIST", fromId: from.id, toId: dest.id, mediaId: mediaId });
      toast("Moved to " + dest.name);
    } else {
      dispatch({ type: "ADD_TO_PLAYLIST", playlistId: dest.id, mediaId: mediaId });
      toast("Added to " + dest.name);
    }
  });

  window.addEventListener("resize", closeRowMenu);

  /* ---- 8.11 playlist ⋯ menu ---- */
  dom.queueMenuBtn.addEventListener("click", function (event) {
    event.stopPropagation();
    toggleQueueMenu();
  });

  dom.queueMenu.addEventListener("click", function (event) {
    var item = event.target.closest(".menu-item");
    if (!item || item.disabled) return;
    var pl = activeUserPlaylist();
    toggleQueueMenu(false);

    switch (item.dataset.action) {
      case "modify":
        openModal("modify");
        break;
      case "rename":
        openModal("rename");
        break;
      case "duplicate":
        if (pl) {
          dispatch({ type: "DUPLICATE_PLAYLIST", id: pl.id });
          toast("Playlist duplicated");
        }
        break;
      case "delete":
        if (pl && window.confirm('Delete the playlist "' + pl.name + '"?')) {
          dispatch({ type: "DELETE_PLAYLIST", id: pl.id });
          toast("Playlist deleted");
        }
        break;
    }
  });

  document.addEventListener("click", function (event) {
    if (!dom.queueMenu.hidden) toggleQueueMenu(false);
    if (!dom.rowMenu.hidden && !event.target.closest("#mx-row-menu")) closeRowMenu();
  });

  /* ---- 8.12 modal ---- */
  dom.modalClose.addEventListener("click", closeModal);
  dom.modalCancel.addEventListener("click", closeModal);
  dom.modalBackdrop.addEventListener("click", closeModal);
  dom.modalSave.addEventListener("click", saveModal);
  dom.modalInput.addEventListener("keydown", function (event) {
    if (event.key === "Enter") saveModal();
  });

  /* ======================================================================
     9. KEYBOARD SHORTCUTS
     Space play/pause · arrows seek · N next · P previous · M mute · F full
     ====================================================================== */

  document.addEventListener("keydown", function (event) {
    var tag = (event.target.tagName || "").toLowerCase();
    var typing = tag === "input" || tag === "select" || tag === "textarea";

    if (event.key === "Escape") {
      if (!dom.modal.hidden) closeModal();
      if (!dom.queueMenu.hidden) toggleQueueMenu(false);
      closeRowMenu();
      return;
    }

    if (typing || !dom.modal.hidden) return;

    /* Shift + >  /  Shift + <  step the playback speed through a fixed
       ladder, exactly like the desktop players people are used to. */
    if (event.shiftKey && (event.key === ">" || event.key === "<" ||
        event.key === "." || event.key === ",")) {
      event.preventDefault();
      var ladder = [0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2];
      var faster = event.key === ">" || event.key === ".";
      var at = ladder.indexOf(state.speed);
      if (at === -1) at = ladder.indexOf(1);
      var next = ladder[Math.max(0, Math.min(ladder.length - 1, at + (faster ? 1 : -1)))];
      dispatch({ type: "SET_SPEED", value: next });
      toast("Speed " + next + "x");
      return;
    }

    switch (event.key) {
      case " ":
        event.preventDefault();
        dispatch({ type: "TOGGLE" });
        break;
      case "ArrowRight":
        dispatch({ type: "SKIP", seconds: 5 });
        break;
      case "ArrowLeft":
        dispatch({ type: "SKIP", seconds: -5 });
        break;
      case "ArrowUp":
        event.preventDefault();
        dispatch({ type: "SET_VOLUME", value: state.volume + 5 });
        break;
      case "ArrowDown":
        event.preventDefault();
        dispatch({ type: "SET_VOLUME", value: state.volume - 5 });
        break;
      case "n":
      case "N":
        dispatch({ type: "NEXT" });
        break;
      case "p":
      case "P":
        dispatch({ type: "PREV" });
        break;
      case "m":
      case "M":
        dispatch({ type: "TOGGLE_MUTE" });
        break;
      case "f":
      case "F":
        toggleFullscreen();
        break;
      case "l":
      case "L":
        // loop / repeat the current queue
        dispatch({ type: "TOGGLE_REPEAT" });
        toast(state.repeat ? "Loop on" : "Loop off");
        break;
    }
  });

  /* ======================================================================
     10. CLOCK — simulated playback
     DELETE THIS WHOLE SECTION once the C/C++ engine reports real positions;
     just keep calling dispatch({ type: "TICK", delta }) from the engine.
     ====================================================================== */

  setInterval(function () {
    if (state.isPlaying && !hasBrowserSource(currentMedia())) {
      dispatch({ type: "TICK", delta: 0.25 });
    }
  }, 250);

  /* ======================================================================
     11. SESSION SAVE FILE  (the ".sav" of this app)
     ----------------------------------------------------------------------
     Everything the user does — theme, volume, mute, speed, shuffle, repeat,
     equalizer (on/off + preset + band gains), the open playlist, the current
     track and its position, the whole library and all playlists — is written
     into ONE json "save file" that lives in the browser's localStorage under
     SAVE_KEY. It is rewritten (debounced) after every command, and read back
     on boot, so reopening the app restores the exact previous state.

     Imported file bytes and (where supported) file handles are stored in
     IndexedDB. This is necessary because blob: URLs expire when a tab closes.
     The browser owns this private storage; a normal website cannot silently
     retain an unrestricted absolute path on the user's hard drive.
     ====================================================================== */

  var SAVE_KEY = "mediax.session.sav";   // change this to reset all saves
  var SAVE_VERSION = 1;
  var saveTimer = null;
  var MEDIA_DB = "mediax-media-files";
  var MEDIA_STORE = "files";

  function openMediaDatabase() {
    return new Promise(function (resolve, reject) {
      if (!window.indexedDB) return reject(new Error("IndexedDB unavailable"));
      var request = indexedDB.open(MEDIA_DB, 1);
      request.onupgradeneeded = function () {
        if (!request.result.objectStoreNames.contains(MEDIA_STORE)) {
          request.result.createObjectStore(MEDIA_STORE, { keyPath: "key" });
        }
      };
      request.onsuccess = function () { resolve(request.result); };
      request.onerror = function () { reject(request.error); };
    });
  }

  async function storeMediaFile(key, file, handle) {
    try {
      var db = await openMediaDatabase();
      await new Promise(function (resolve, reject) {
        var transaction = db.transaction(MEDIA_STORE, "readwrite");
        transaction.objectStore(MEDIA_STORE).put({ key: key, file: file, handle: handle || null });
        transaction.oncomplete = resolve;
        transaction.onerror = function () { reject(transaction.error); };
      });
      db.close();
      if (navigator.storage && navigator.storage.persist) navigator.storage.persist().catch(function () {});
    } catch (err) {
      toast("This browser could not save the media file for the next session.");
    }
  }

  async function readStoredMedia(key) {
    try {
      var db = await openMediaDatabase();
      var record = await new Promise(function (resolve, reject) {
        var request = db.transaction(MEDIA_STORE, "readonly").objectStore(MEDIA_STORE).get(key);
        request.onsuccess = function () { resolve(request.result || null); };
        request.onerror = function () { reject(request.error); };
      });
      db.close();
      if (!record) return null;
      if (record.handle && typeof record.handle.queryPermission === "function") {
        var permission = await record.handle.queryPermission({ mode: "read" });
        if (permission === "granted") return record.handle.getFile();
      }
      return record.file || null;
    } catch (err) { return null; }
  }

  /** Permanently remove a deleted import from the browser's media store. */
  async function deleteStoredMedia(key) {
    try {
      var db = await openMediaDatabase();
      await new Promise(function (resolve, reject) {
        var transaction = db.transaction(MEDIA_STORE, "readwrite");
        transaction.objectStore(MEDIA_STORE).delete(key);
        transaction.oncomplete = resolve;
        transaction.onerror = function () { reject(transaction.error); };
      });
      db.close();
    } catch (err) {
      /* The library entry is still deleted even if browser storage is locked. */
    }
  }

  /** Recreate fresh blob URLs for imported media before the first render. */
  async function restoreStoredMedia() {
    await Promise.all(LIBRARY.map(async function (media) {
      if (!media.storageKey) return;
      var file = await readStoredMedia(media.storageKey);
      if (!file) return;
      media.objectUrl = URL.createObjectURL(file);
      if (!media.byteSize) media.byteSize = file.size;
      if (!media.bitrate || media.bitrate === "unknown") {
        if (!media.duration || media.duration === 180) {
          media.duration = (await probeDuration(file, media.kind === "video")) || media.duration;
        }
        media.bitrate = formatBitrate(media.byteSize, media.duration);
      }
      if (!media.art || media.art.indexOf("blob:") === 0 ||
          media.art.indexOf("data:image/svg+xml") === 0 ||
          media.artworkVersion !== ARTWORK_VERSION) {
        media.art = await artworkFromFile(file, media.kind === "video");
        media.artworkVersion = ARTWORK_VERSION;
      }

    }));
  }

  /** Build the plain object that gets serialised into the save file. */
  function buildSave() {
    return {
      version: SAVE_VERSION,
      savedAt: new Date().toISOString(),
      library: LIBRARY.map(function (media) {
        var copy = {};
        Object.keys(media).forEach(function (key) { copy[key] = media[key]; });
        // blob: urls cannot survive a reload — do not store them
        if (typeof copy.objectUrl === "string" && copy.objectUrl.indexOf("blob:") === 0) {
          delete copy.objectUrl;
        }
        return copy;
      }),
      playlists: PLAYLISTS,
      playlistSeq: playlistSeq,
      state: {
        theme: state.theme,
        source: state.source,
        currentId: state.currentId,
        position: state.position,
        volume: state.volume,
        muted: state.muted,
        speed: state.speed,
        shuffle: state.shuffle,
        repeat: state.repeat,
        recent: state.recent,
        eqOn: state.eqOn,
        eqPreset: state.eqPreset,
        eqGains: state.eqGains,
      },
    };
  }

  /** Write the save file (debounced so a TICK storm cannot spam the disk). */
  function saveSession() {
    if (saveTimer) return;
    saveTimer = setTimeout(function () {
      saveTimer = null;
      try {
        localStorage.setItem(SAVE_KEY, JSON.stringify(buildSave()));
      } catch (err) {
        /* quota full / private mode — the app keeps working, just unsaved */
      }
    }, 400);
  }

  /** Read the save file back into LIBRARY / PLAYLISTS / state. */
  function loadSession() {
    var raw = null;
    try { raw = localStorage.getItem(SAVE_KEY); } catch (err) { return; }
    if (!raw) return;

    var save;
    try { save = JSON.parse(raw); } catch (err) { return; }
    if (!save || save.version !== SAVE_VERSION) return;

    if (Array.isArray(save.library)) {
      LIBRARY.length = 0;
      save.library.forEach(function (media) { LIBRARY.push(media); });
    }

    if (Array.isArray(save.playlists) && save.playlists.length) {
      PLAYLISTS.length = 0;
      save.playlists.forEach(function (pl) {
        PLAYLISTS.push({ id: pl.id, name: pl.name, items: (pl.items || []).slice() });
      });
      playlistSeq = save.playlistSeq || PLAYLISTS.length;
    }

    if (save.state) {
      Object.keys(save.state).forEach(function (key) {
        if (save.state[key] !== undefined) state[key] = save.state[key];
      });
    }

    // never resume "playing" automatically — browsers block it anyway
    state.isPlaying = false;
    state.search = "";
    if (state.currentId && !findMedia(state.currentId)) {
      state.currentId = LIBRARY.length ? LIBRARY[0].id : null;
      state.position = 0;
    }
    state.recent = (state.recent || []).filter(function (id) { return !!findMedia(id); });
  }

  /* Save after every command, and once more when the tab goes away. */
  var dispatchWithoutSave = dispatch;
  dispatch = function (command) {
    dispatchWithoutSave(command);
    saveSession();
  };
  window.addEventListener("beforeunload", function () {
    try { localStorage.setItem(SAVE_KEY, JSON.stringify(buildSave())); } catch (err) {}
  });

  /* ======================================================================
     12. BOOT
     ====================================================================== */

  /** Build the equalizer bars once — they are animated purely with CSS. */
  function buildBars() {
    var eqBars = "";
    for (var j = 0; j < 22; j++) {
      eqBars += '<span style="animation-delay:' + j * 70 + 'ms"></span>';
    }
    dom.eq.innerHTML = eqBars;
  }

  async function boot() {
    loadSession();
    await restoreStoredMedia();
    buildBars();
    buildEqBands();
    renderAll();
    wakeChrome();
  }

  boot();


  /* Expose a tiny debug/bridge handle so the future native layer (or the
     browser console) can drive the UI:  MediaX.dispatch({type:"PLAY"})   */
  window.MediaX = {
    dispatch: dispatch,
    getState: function () {
      return JSON.parse(JSON.stringify(state));
    },
    getPlaylists: function () {
      return PLAYLISTS;
    },
  };
})();
