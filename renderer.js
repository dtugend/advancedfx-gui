{
    const strIn = document.getElementById("strIn");
    const strOut = document.getElementById("strOut");
    const strSend = document.getElementById("strSend");

    strSend.onclick = function(ev){
        console.log(window.advancedfx);
        window.advancedfx.jsonRequest(strIn.value).then((result)=>{
            strOut.value = result;
        });
    }
}