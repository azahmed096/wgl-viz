const THREE = require('three');
import Client from './client';
import cloneGltf from './hack';

var scene = new THREE.Scene();
var camera = new THREE.PerspectiveCamera(
    75, window.innerWidth / window.innerHeight, 0.1, 1000);
var renderer = new THREE.WebGLRenderer();
renderer.setSize(window.innerWidth, window.innerHeight);
document.body.appendChild(renderer.domElement);

// LIGHT
var light = new THREE.AmbientLight(0x404040);
scene.add(light);

var geometry = new THREE.BoxGeometry(1, 1, 1);
var material = new THREE.MeshBasicMaterial({ color: 0x00ff00 });
var cube = new THREE.Mesh(geometry, material);
scene.add(cube);
camera.position.z = 50;

var uri = document.getElementById("uri");
var websocket;


function animate() {
    requestAnimationFrame(animate);
    cube.rotation.x += 0.01;
    cube.rotation.y += 0.01;
    renderer.render(scene, camera);
}

const LOADED_GLTF = [];

function load(e) {
    LOADED_GLTF.push(e);
}

function msg(id, ...rest) {
    let a = cloneGltf(LOADED_GLTF[id]);
    a.scene.position.set(...rest);
    scene.add(a.scene);
}

function connect() {
    console.log("OOO");
    if (websocket) {
        websocket.websocket.close();
    }
    websocket = new Client(uri.value, load, msg);
}
console.log("ooo");
document.getElementById("connect").addEventListener("click", connect);

animate();