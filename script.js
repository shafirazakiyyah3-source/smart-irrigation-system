async function updateSensor(){

const res = await fetch('/api/sensors');

const data = await res.json();

document.getElementById("temp").innerHTML=data.temp.toFixed(1)+" °C";

document.getElementById("hum").innerHTML=data.hum.toFixed(1)+" %";

document.getElementById("soil").innerHTML=data.soil+" %";

document.getElementById("fanState").innerHTML=
"Status : "+(data.relay1?"ON":"OFF");

document.getElementById("pumpState").innerHTML=
"Status : "+(data.relay3?"ON":"OFF");

}

async function relay(id,state){

await fetch('/api/relay',{

method:'POST',

headers:{

'Content-Type':'application/json'

},

body:JSON.stringify({

relay:id,

state:state

})

});

updateSensor();

}

setInterval(updateSensor,1000);

updateSensor();
