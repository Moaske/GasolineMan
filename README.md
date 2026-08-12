# GasolineMan
Very basic Pebble watch app that reads three Fuel price sensors that I've setup in my Home Assiatant.
I live close to the Belgian border, so need to gauge Belgian fuel prices compared to dutch prices ... ;-)

Emery only (Time 2/Obelix)

<img src="https://github.com/Moaske/GasolineMan/blob/main/docs/gasolineman_main.png"></img>&nbsp;&nbsp;&nbsp;<img src="https://github.com/Moaske/GasolineMan/blob/main/docs/gasolineman_graph.png"></img>

Feel free to fork it to your own needs.

## Examples of HA sensors with all required atrributes

```yaml
template:
  - sensor:
      - name: "CBS Petrol Euro95"
        state_class: measurement
        unit_of_measurement: "€/L"
        icon: mdi:gas-station
        state: >
            {{ state_attr('sensor.cbs_petrol_raw', 'value')[-1].BenzineEuro95_1 }}
```

```yaml
template:
  - sensor:
      - name: "VanRaak Euro95 (clean)"
        unique_id: vanraak_euro95_clean
        icon: mdi:gas-station
        state: >
          {% set raw = states('sensor.vanraak_poppel_benzine') %}
          {% if '€' in raw %}
            {{ raw.split('€')[1] | replace('\t','') | replace('\n','') | trim }}
          {% else %}
            unknown
          {% endif %}
        unit_of_measurement: "€/L"
        state_class: measurement
```
