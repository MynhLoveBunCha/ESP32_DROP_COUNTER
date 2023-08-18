var gateway = 'ws://' + window.location.hostname + ':81/';
var websocket;

function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage; // <-- add this line
}

function onOpen(event) {
    console.log('Connection opened');
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}

function onMessage(event) {
    var obj = JSON.parse(event.data);
    document.getElementById("count").innerHTML = obj.count;
}

window.addEventListener('load', onLoad);

function onLoad(event) {
    initWebSocket();
    initButton();
}

function initButton() {
    ['reset', 'stop', 'increase', 'decrease', 'start'].forEach(element => {
        document.getElementById(element).addEventListener('click', function () {
            button_send_back(element);
        });
    });
}

function button_send_back(command) {
    var msg = {
        command: command
    };
    websocket.send(JSON.stringify(msg));
}
