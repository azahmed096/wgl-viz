import GLTFLoader from 'three-gltf-loader';


export default class Client {
    constructor(uri, loadCallback, messageCallback) {
        this.websocket = new WebSocket(uri, "spawn-objects" /* libwebsocket protocol */);
        this.websocket.onopen = this.onOpen.bind(this);
        this.websocket.onclose = this.onClose.bind(this);
        this.websocket.onmessage = this.firstMessage.bind(this);

        // Same uri as given but with http
        // wil be used to load models
        let url = new URL(uri);
        url.protocol = "http";
        this.url = url;
        this.loadCallback = loadCallback;
        this.messageCallback = messageCallback;
    }

    onOpen(evt) {
        console.log("open");
    }

    onClose(evt) {
        console.log("close");
    }

    firstMessage(evt) {
        console.log(`First message ${evt.data}`);
        evt.data.split('|').forEach((path) => {
            console.log(`Foreach (${path})`);
            this.url.pathname = path;
            var loader = new GLTFLoader();
            loader.load(this.url.href, this.loadCallback);
        });
        this.websocket.onmessage = this.onMessage.bind(this);
    }

    onMessage(evt) {
        let reader = new FileReader();
        reader.onload = (e) => {
            let arrayBuffer = e.target.result;
            let dataView = new DataView(arrayBuffer);
            let id = dataView.getInt32(0, true);
            let pos = new Float32Array(arrayBuffer, 4);
            console.log(`Got message id=${id}`);
            console.log(pos);
            this.messageCallback(id, ...pos);
        };
        reader.readAsArrayBuffer(evt.data);
    }
}