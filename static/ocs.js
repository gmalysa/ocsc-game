var ocs = {};
ocs.$ = {};

ocs.$.onReady = function(f) {
	if (document.readyState != 'loading')
		f();
	else
		document.addEventListener('DOMContentLoaded', f);
};

/**
 * Show username/uuid from cookies in case they didn't write it down.
 * The inline html kind of sucks but pulling in a whole templating engine just to
 * generate a couple of small snippets is crazy
 */
ocs.$.onReady(async function() {
	const user = await cookieStore.get("userid");
	const display = await cookieStore.get("userdisplay");
	let s1 = "";

	if (user && display) {
		s1 = `Your most recent user is <span class="highlight">${display.value}</span><br/>with uuid <span class="highlight">${user.value}</span>`;
	}
	else {
		s1 = `Set a display name to get a user id: <input type="text" id="regname" /><input type="button" onclick="register()" value="Get UUID" />`;
	}

	document.getElementById("userinfo").innerHTML = s1;
	document.getElementsByName("user_uuid").forEach(function(e) {
		e.innerHTML = user.value;
	});
});

/**
 * register username
 */
function register() {
	const name = document.getElementById("regname").value;
	const url = "/game/new-user?name="+name;
	const args = {
		method : 'GET',
		credentials: 'same-origin',
		headers : {
			'Content-Type': 'application/json',
			'Accept' : 'application/json'
		},
	};

	return fetch(url, args).then(function(r) { window.location.reload(); });
}
