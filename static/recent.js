
async function load_recents() {
	let userid = window.location.hash.substr(1);

	if (userid.length < 3 && isNaN(parseInt(userid))) {
		await show_games();
	}
	else {
		await show_user_games(userid);
	}
}

async function show_games() {
	let games = await ocs.$.fetch_json('/game/recent-games');
	if (games.error) {
		// ...
		return;
	}

	let strings = [];
	strings.push(`<p>Recent games among all players<ul>`);
	games.games.forEach(function(g) {
		if ("id" in g)
			strings.push(format_game(g));
	});
	strings.push('</ul></p>');

	document.getElementById('recents').innerHTML = strings.join('');
}

function format_game(g) {
	return `<li><a href="/game.html#${g.id}">Game ${g.id}: ${ocs.$.game_status_string(g)}</a> [type <span class="goal_const">${g.type}</span>, seen <span class="goal_const">${g.count}</span>, accepted <span class="goal_const">${g.accepted}</span>/1000]</li>`;
}

async function show_user_games(user) {
	let games = await ocs.$.fetch_json('/game/user-games?name='+user);
	if (games.error) {
		// ...
		return;
	}

	let strings = [];
	strings.push(`<p>Recent games for user ${user}<ul>`);
	games.games.forEach(function(g) {
		if ("id" in g)
			strings.push(format_game(g));
	});
	strings.push('</ul></p>');

	document.getElementById('recents').innerHTML = strings.join('');
}

ocs.$.onReady(async function() {
	await load_recents();
});

function find_user_games() {
	window.location = '/recent.html#'+document.getElementById('userid').value;
	load_recents();
}

ocs.$.link_enter('userid', find_user_games);
