/*
 * Live image tuning on the settings page.
 *
 * The streamer applies image settings when they are saved, so choosing a
 * brightness means saving, looking, adjusting and saving again. Where a plugin
 * is installed the same knobs can be set on the running ISP directly, so the
 * picture follows the slider and the value can be chosen by eye.
 *
 * Nothing here replaces saving. The plugin sets the ISP; the configuration is
 * still whatever was last saved, and Save is still what makes a choice stick.
 * A slider moved and then abandoned leaves the picture where it was dragged to
 * until the streamer next applies its own settings - which is the honest
 * behaviour of a preview, and the reason the Save button is not touched.
 *
 * Deliberately a separate file that binds nothing at load time. The settings
 * form is built by mj-settings.js after its own fetches complete and is rebuilt
 * whenever the tab changes, so anything that attached to the inputs directly
 * would have to know when that happened. Listening on the document for input
 * events that bubble sidesteps all of it: there is no ordering to get right, no
 * observer to install, and mj-settings.js is not touched - which matters, since
 * it ships minified and a change there cannot be reviewed in a diff.
 */
(function () {
	"use strict";

	/* Setting path -> plugin command. Only the knobs the plugin can actually
	 * set; anything else falls through and behaves exactly as before. */
	var KNOBS = {
		"image.luminance": "brightness",
		"image.contrast": "contrast",
		"image.saturation": "saturation",
		"image.hue": "hue"
	};

	/* The plugin's knobs are 0-255, the range libimp's own calls use. The
	 * slider is on whatever range the streamer's schema declares, so the
	 * mapping is taken from the element rather than assumed - if the schema
	 * ever changes, this follows it instead of quietly sending wrong numbers. */
	var ISP_MAX = 255;

	/* Dragging a slider fires continuously. Coalesce to one request in flight
	 * per interval, keeping only the latest value per knob, and always send the
	 * final position - a preview that stops one step short of where the slider
	 * was left would be worse than none. */
	var INTERVAL = 120;
	var pending = {};
	var inflight = false;
	var timer = null;

	function send() {
		timer = null;

		var cmd = null;
		for (var k in pending) {
			if (Object.prototype.hasOwnProperty.call(pending, k)) {
				cmd = k;
				break;
			}
		}

		if (cmd === null) {
			return;
		}

		var val = pending[cmd];
		delete pending[cmd];
		inflight = true;

		fetch("/cgi-bin/j/plugin.cgi?cmd=" + encodeURIComponent(cmd) +
			"&val=" + encodeURIComponent(val), { credentials: "same-origin" })
			.catch(function () { /* no plugin, or it refused: leave the UI alone */ })
			.then(function () {
				inflight = false;
				schedule();
			});
	}

	function schedule() {
		if (timer !== null || inflight) {
			return;
		}

		for (var k in pending) {
			if (Object.prototype.hasOwnProperty.call(pending, k)) {
				timer = setTimeout(send, INTERVAL);
				return;
			}
		}
	}

	document.addEventListener("input", function (e) {
		var el = e.target;
		if (!el || !el.id || el.id.lastIndexOf("mjf-", 0) !== 0) {
			return;
		}

		/* mj-settings.js names every field mjf- plus the setting path with its
		 * dots turned into dashes. */
		var cmd = KNOBS[el.id.slice(4).replace(/-/g, ".")];
		if (!cmd) {
			return;
		}

		var value = parseFloat(el.value);
		if (!isFinite(value)) {
			return;
		}

		var min = parseFloat(el.min);
		var max = parseFloat(el.max);
		if (!isFinite(min)) {
			min = 0;
		}
		if (!isFinite(max) || max <= min) {
			max = 100;
		}

		var scaled = Math.round((value - min) / (max - min) * ISP_MAX);
		pending[cmd] = scaled < 0 ? 0 : (scaled > ISP_MAX ? ISP_MAX : scaled);
		schedule();
	}, true);
})();
