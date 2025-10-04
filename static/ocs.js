var ocs = {};
ocs.$ = {};

ocs.$.onReady = function(f) {
	if (document.readyState != 'loading')
		f();
	else
		document.addEventListener('DOMContentLoaded', f);
};

ocs.$.fetch_json = async function(url) {
	const args = {
		method : 'GET',
		headers : {
			'Content-Type': 'application/json',
			'Accept' : 'application/json'
		},
	};

	let response = await fetch(url, args);
	return await response.json();
}

ocs.$.link_enter = function(src, fn) {
	ocs.$.onReady(function() {
		document.getElementById(src).addEventListener("keypress", function(k) {
			if (k.keyCode == 13)
				fn();
		});
	});
}
