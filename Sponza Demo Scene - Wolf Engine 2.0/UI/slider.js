var template = document.createElement('template');
template.innerHTML = `
    <section class="range-slider">
    </section>
`;

class WolfSlider extends HTMLElement {
    constructor() {
        super();
        this.appendChild(template.content.cloneNode(true));
        this.min = this.getAttribute('min')?this.getAttribute('min'):"0";
        this.max = this.getAttribute('max')?this.getAttribute('max'):"100";
        this.step = this.getAttribute('step')?this.getAttribute('step'): (this.min + this.max)/100;
        this.values = this.getAttribute("value")? this.getAttribute("value").split(";").map((str)=>Number(str)) : [(this.min + this.max)/2];
        var div = document.createElement('div');
        div.style = "margin: auto 0 auto 1rem;"
        div.innerHTML=`${this.values}`;
        this.appendChild(div)
        this.values.forEach((val, index) => {
            this.querySelector('section').appendChild(this.input({
                type: "range",
                value: val,
                key: index,
                ariaValueNow: val,
                ariaValueText: val,
                ariaOrientation: "horizontal",
                ariaValueMax: this.max,
                ariaValueMin: this.min,
                min: this.min,
                max: this.max,
                step:this.step
            }))
        })
    }

    input({ key, value, ...props }) {
        const el = document.createElement("input");
        Object.assign(el, props);
        el.setAttribute("value", value);
        el.addEventListener('input', () => {
            this.values[key] = Number(el.value);
            this.querySelector('div').innerHTML=`${this.values}`;
            window[this.getAttribute('oninput')](this.values.length == 1 ? this.values[0] : this.values);
        })
        return el;
    }

    disconnectedCallback() {
        this.removeEventListener('change');
    }
}

customElements.define("wolf-slider", WolfSlider);