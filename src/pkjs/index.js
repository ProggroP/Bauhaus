// PebbleKit JS: reicht nur die Clay-Einstellungen an die Uhr weiter.
// Alles, was angezeigt wird -- Zeit, Datum, Schritte, Ladestand -- liest die
// Uhr selbst. Deshalb genuegt hier Clays automatischer Handler.

var Clay = require('@rebble/clay');
var clayConfig = require('./config');

var clay = new Clay(clayConfig);
