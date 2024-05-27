#!/usr/bin/env -S deno run -A

const totalMemoryDumps = 7;

let allDiffs: { [addr: number]: number[] } = {};

// process first two

const memoryDump0 = await Deno.readFile(`build/memory0.bin`);
const memoryDump1 = await Deno.readFile(`build/memory1.bin`);

for (
	let addr = 0;
	addr < Math.min(memoryDump0.length, memoryDump1.length);
	addr++
) {
	const a = memoryDump0[addr];
	const b = memoryDump1[addr];

	if (a != b) {
		allDiffs[addr] = [a, b];
	}
}

// process rest

for (
	let memoryDumpIndex = 2;
	memoryDumpIndex < totalMemoryDumps;
	memoryDumpIndex++
) {
	const memoryDump = await Deno.readFile(
		`build/memory${memoryDumpIndex}.bin`,
	);

	for (const _address of Object.keys(allDiffs)) {
		const address = Number(_address);
		const value = memoryDump[address];

		if (allDiffs[address] == null) continue;

		if (allDiffs[address].slice(-1)[0] == value) {
			// last value is same so delete diff
			delete allDiffs[address];
			continue;
		}

		allDiffs[address].push(value);
	}
}

// filter

let filteredDiffs: { [addr: number]: number[] } = {};

for (const _address of Object.keys(allDiffs)) {
	const address = Number(_address);
	const diffs = allDiffs[address];

	let good = true;
	for (let value of diffs) {
		if (value != 0 && value != 1) {
			good = false;
			break;
		}
	}

	if (good) {
		filteredDiffs[address] = diffs;
	}
}

let out = "";

for (const [address, diffs] of Object.entries(filteredDiffs)) {
	out += Number(address).toString(16) + ": " + diffs.join(", ") + "\n";
}

await Deno.writeTextFile("./changes.txt", out);
