async function load_game() {
	let game = window.location.hash.substr(1);

	// Game can be specified as an integer id (public) or a uuid (private)
	// and the server will do proper validation so we're mostly trying to
	// spot that no game has been specified vs. maybe a game has been specified
	if (game.length < 36 && isNaN(parseInt(game))) {
		show_error();
		return;
	}

	const url = "/game/details?game="+game;
	let json = await ocs.$.fetch_json(url);

	if (json.error) {
		show_error();
	}
	else {
		await render_game(game, json);
	}
}

function show_error() {
	let msg = `No game or invalid game ID provided. Enter one below or go back to the list`;
	document.getElementById('details').innerHTML = msg;
}

async function get_game_params(type) {
	const url = "/game/params?type="+type;
	return await ocs.$.fetch_json(url);
}

const GOAL_OP_BIT = 1 << 13;
const GOAL_ATTR_BIT = 1 << 12;

function map_to_operator(g) {
	g = g & ~GOAL_OP_BIT;
	switch (g) {
	case 0: return '+';
	case 1: return '-';
	case 2: return '/';
	case 3: return '*';
	case 4: return '<';
	case 5: return '>=';
	default: return '#unknown#';
	}
}

function render_goal_line(goal, game) {
	return goal.map(function(g) {
		if (g & GOAL_OP_BIT)
			return `<span class="goal_op">${map_to_operator(g)}</span>`;
		if (g & GOAL_ATTR_BIT)
			return `<span class="goal_attr">attr[${g & ~GOAL_ATTR_BIT}]</span>`;
		return `<span class="goal_const">${g}</span>`;
	}).join(' ');
}

function goal_val(g, game) {
	if (g & GOAL_ATTR_BIT) {
		let idx = g & ~GOAL_ATTR_BIT;
		return game.attrs[idx];
	}
	return g;
}

function apply_op(triple, game) {
	let op = triple[0] & ~GOAL_OP_BIT;
	let a1 = goal_val(triple[1], game);
	let a2 = goal_val(triple[2], game);

	switch(op) {
	case 0: return a1 + a2;
	case 1: return a1 - a2;
	case 2: return a1 / a2;
	case 3: return a1 * a2;
	case 4: return a1 < a2;
	case 5: return a1 >= a2;
	default: return -1;
	}
}

function goal_eval_one(goal, game) {
	if (goal.length == 1)
		return goal;

	for (i = 0; i < goal.length; ++i) {
		if (goal[i] & GOAL_ATTR_BIT) {
			goal[i] = goal_val(goal[i], game);
			return goal;
		}
	}

	for (i = 0; i < goal.length-2; ++i) {
		let c1 = (goal[i] & GOAL_OP_BIT) == GOAL_OP_BIT;
		let c2 = (goal[i+1] & GOAL_OP_BIT) != GOAL_OP_BIT;
		let c3 = (goal[i+2] & GOAL_OP_BIT) != GOAL_OP_BIT;
		if (c1 && c2 && c3) {
			goal.splice(i, 3, apply_op(goal.slice(i, 3), game));
			return goal;
		}
	}
}

function render_goal(goal, game) {
	let strings = [];

	while (goal.length > 1) {
		strings.push('<div class="goal_line">');
		strings.push(render_goal_line(goal, game));
		strings.push('</div>');
		goal = goal_eval_one(goal, game);
	}

	strings.push('<div class="goal_result">');
	if (goal[0])
		strings.push('<span class="goal_true">true</span>');
	else
		strings.push('<span class="goal_false">false</span>');
	strings.push('</div>');

	return strings.join('');
}

const SYM_ACCEPT_BIT = 1 << 7;
const MAX_SYM = 128;
const MAX_ATTR = 7;

async function get_game_symbols(id) {
	const url = '/game/symbols?game='+id;
	return await ocs.$.fetch_json(url);
}

function remap_symbol(s) {
	// First use a-z
	if (s < 26)
		return String.fromCodePoint(s + 97);
	s = s - 26;

	// Then use A-Z and following symbols
	if (s < 32)
		return String.fromCodePoint(s + 65);
	s = s - 32;

	// Then use symbols + numbers below A-Z
	if (s < 32)
		return String.fromCodePoint(s + 33);
	s = s - 32;

	// Then use a couple of spares at the end of ascii
	if (s < 4)
		return String.fromCodePoint(s + 123);
	s = s - 4;

	// Then lowercase greek letters
	if (s < 25)
		return String.fromCodePoint(s + 945);
	s = s - 25;

	// Finally out of 128 currently possible symbols, the last 9 are emoji
	return String.fromCodePoint(s + 0x1f600);
}

function symbol_to_attrs(sym) {
	let attrs = new Array(MAX_ATTR).fill(0, 0);
	for (i = 0; i < MAX_ATTR; ++i) {
		if (sym & (1 << i))
			attrs[i] = 1;
	}
	return `[${attrs}]`;
}

async function render_game(id, game) {
	let params = await get_game_params(game.type);

	let strings = [];
	strings.push(`<table>`);
	strings.push(`<tr><td class="tlabel">Game</td><td>${id}</td></tr>`);
	strings.push(`<tr><td class="tlabel">Type</td><td>${game.type}</td></tr>`);
	strings.push(`<tr><td class="tlabel">Status</td><td>${ocs.$.game_status_string(game)}</td></tr>`);
	strings.push(`<tr><td class="tlabel">Accepted</td><td><span class="goal_const">${game.accepted}</span></td></tr>`);
	strings.push(`<tr><td class="tlabel">Rejected</td><td><span class="goal_const">${game.count-game.accepted}</span></td></tr>`);

	strings.push('<tr><td class="tlabel">Attrs</td><td>');
	game.attrs.forEach(function(a, i) {
		strings.push(`<div><span class="goal_attr">[${i}]</span> = <span class="goal_const">${a}</span></div>`);
	});
	strings.push('</td></tr>');

	params.goals.forEach(function(g, i) {
		strings.push(`<tr><td class="tlabel">Goal ${i}</td><td>`);
		strings.push(render_goal(g, game));
		strings.push('</td></tr>');
	});

	let symbols = await get_game_symbols(id);
	let symcounts = new Array(MAX_SYM).fill(0, 0);
	let asymcounts = new Array(MAX_SYM).fill(0, 0);
	strings.push('<tr><td class="tlabel">Symbols</td><td class="symbols">');
	strings.push(symbols.symbols.map(function(s) {
		let accepted = s & SYM_ACCEPT_BIT;
		s = s & ~SYM_ACCEPT_BIT;
		symcounts[s] = symcounts[s] + 1;
		let sym = remap_symbol(s);
		if (accepted) {
			asymcounts[s] = asymcounts[s] + 1;
			return `<span class="accepted">${sym}</span>`;
		}
		return `<span class="rejected">${sym}</span>`;
	}).join(''));

	strings.push('<tr><td class="tlabel">Stats</td><td>');
	symcounts.forEach(function(s, i) {
		if (s > 0) {
			strings.push(`<div><span class="goal_const">${remap_symbol(i)}</span>: <span class="accepted">${asymcounts[i]}</span> + <span class="rejected">${s - asymcounts[i]}</span> = ${s}</div>`);
		}
	});
	strings.push('</td></tr>');

	strings.push('<tr><td class="tlabel">Legend</td><td>');
	symcounts.forEach(function(s, i) {
		if (s > 0) {
			strings.push(`<div><span class="goal_const">${remap_symbol(i)}</span>: ${i} = ${symbol_to_attrs(i)}</div>`);
		}
	});
	strings.push('</td></tr>');

	document.getElementById('details').innerHTML = strings.join('');

	if (!game.finished)
		setTimeout(load_game, 5000);
}

function load_form_game() {
	window.location = '/game.html#'+document.getElementById('gameid').value;
	load_game();
}

ocs.$.onReady(load_game);
ocs.$.link_enter('gameid', load_form_game);
