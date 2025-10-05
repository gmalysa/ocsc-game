/**
 * Show username/uuid from cookies in case they didn't write it down.
 * The inline html kind of sucks but pulling in a whole templating engine just to
 * generate a couple of small snippets is crazy
 */
ocs.$.onReady(async function() {
	const user = await cookieStore.get("userid");
	const display = await cookieStore.get("userdisplay");
	let s1 = "";
	let setkey = false;

	if (user && display) {
		s1 = `Your most recent user is <span class="highlight">${display.value}</span><br/>with uuid <span class="highlight">${user.value}</span>`;
		document.getElementsByName("user_uuid").forEach(function(e) {
			e.innerHTML = user.value;
		});
	}
	else {
		s1 = `Set a display name to get a user id: <input type="text" id="regname" /><input type="button" onclick="register()" value="Get UUID" />`;
		setkey = true;
	}

	document.getElementById("userinfo").innerHTML = s1;

	if (setkey)
		ocs.$.link_enter('regname', register);
});

/**
 * register username
 */
async function register() {
	const name = document.getElementById("regname").value;
	const url = "/game/new-user?name="+name;
	let json = ocs.$.fetch_json(url);
	window.location.reload();
}

/**
 * Load recent game information
 */
ocs.$.onReady(async function() {
	let strings = [];

	let reply = await ocs.$.fetch_json('/game/params');
	console.log(reply);
	strings.push(`There are <span class="goal_const">${reply.rulesets}</span> different game types available. `);

	reply = await ocs.$.fetch_json('/game/gameid');
	strings.push(`<span class="goal_const">${reply.gameid+1}</span> games have been played. <a href="/recent.html">Browse recent games</a>.`);

	document.getElementById('recent_games').innerHTML = strings.join('');
})
