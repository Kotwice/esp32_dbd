$(document).ready( () => {

    let arrays = {"fm": {"new": [], "old": []}, "dac": {"new": [], "old": []}}

    function isValidJsonString(jsonString) {    
        if(!(jsonString && typeof jsonString === "string")){
            return false;
        }    
        try{
           JSON.parse(jsonString);
           return true;
        }catch(error){
            return false;
        }    
    }

    function checkChange(value_old, value_new) {
        let result = {'value': [], 'index': []}
        if (value_old != undefined & value_new != undefined) {
            if (value_old.length == value_new.length) {
                for (let i = 0; i < value_old.length; i++) {
                    if (value_old[i] != value_new[i]) {
                        result.value.push(value_new[i])
                        result.index.push(i)
                    }
                }
            }
        }
        return result
    }

    function isValidParameters(input) {
        let indicator_input = input['indicator-input'], limits = input.limits, length = input.length
        let flag = false
        $('#' + indicator_input).on('input', () => {
            let line = $('#' + indicator_input).val()    
            if (isValidJsonString(line)) {
                let object = JSON.parse(line)    
                if (object.length == length) {
                    for (let index in object) {
                        let value = object[index]
                        flag = (!isNaN(value)) ? ((value >= limits[0] && value <= limits[1]) ? true : false) : false
                    }
                }
                else {
                    flag = false;
                }
            }    
            else {
                flag = false;
            }
            flag ? $('#' + indicator_input).removeClass('is-invalid').addClass('is-valid') : $('#' + indicator_input).removeClass('is-valid').addClass('is-invalid')
        })
    }

    async function fetching (input) {
        let indicator = input['indicator-input'], parameter = input['parameter']
        $('#' + indicator).on('blur', async () => {
            if ($('#' + indicator).hasClass('is-valid')) {
                arrays[parameter]['old'] = arrays[parameter]['new']
                arrays[parameter]['new'] = JSON.parse($('#' + indicator).val())
                let result = checkChange(arrays[parameter]['old'] , arrays[parameter]['new'])
                // input['value_old'] = input['value_new'] 
                // input['value_new'] = JSON.parse($('#' + indicator).val())
                // let result = checkChange(input['value_old'], input['value_new'])
                if (result.index.length != 0) {
                    console.log(input['result'])
                    let object = Object()
                    object[parameter] = result
                    let path = url + '/response?json=' + JSON.stringify(object)
                    console.log(path)
                    await fetch(path).then((response) => {
                        console.log(response)
                    }).catch((error) => {   
                        console.log(error)
                    })
                }
            }
        })
    }

    let url = 'http://192.168.1.1:8080'

    let form = [
        {'indicator-input': 'input-1', 'limits': [8, 200], 'length': 6, 'indicator-button': 'button-1', 'parameter': 'fm'},
        {'indicator-input': 'input-2', 'limits': [0, 4096], 'length': 8, 'indicator-button': 'button-2', 'parameter': 'dac'},
    ]

    function initialaze () {

        fetch(url + '/request').then(async (response) => {
            let values = await response.json()
            console.log(values)
            for (let object of form) {
                $('#' + object['indicator-input']).prop('disabled', false)
            }
            for (let object of form) {
                for (let label in values) {
                    if (label == object.parameter) {
                        let value = '[' + values[label] + ']'
                        arrays[object.parameter]['old'] = JSON.parse(value)
                        arrays[object.parameter]['new'] = JSON.parse(value)
                        $('#' + object['indicator-input']).removeClass('is-invalid').addClass('is-valid')
                        document.getElementById(object['indicator-input']).value = value
                    }
                }  
            }  
        }).catch((error) => {
            for (let object of form) {
                $('#' + object['indicator-input']).prop('disabled', true)
            }
            console.log(error)
        })
    }

    $('#button-request').on('click', () => {
        initialaze()
    })

    initialaze()

    for (let index in form) {
        let input = form[index]
        $('#' + input['indicator-input']).prop('disabled', true)
        isValidParameters(input)
        fetching(input)
    }

})