import { requestCapability } from './bridge'

const peopleBase = '/api/v1/apps/people-counting'

const get = async (url: string) => {
  if (url === `${peopleBase}/config`) return { data: await requestCapability('peopleCounting.config.read') }
  if (url === `${peopleBase}/stats`) return { data: await requestCapability('peopleCounting.stats.read') }
  if (url === `${peopleBase}/backlog`) return { data: await requestCapability('peopleCounting.backlog.read') }
  throw Object.assign(new Error('MOCK_ROUTE_NOT_ALLOWED'), { code: 'MOCK_ROUTE_NOT_ALLOWED' })
}

const post = async (url: string, payload?: unknown) => {
  if (url === `${peopleBase}/config`) return { data: await requestCapability('peopleCounting.config.write', payload) }
  if (url === `${peopleBase}/reset`) return { data: await requestCapability('peopleCounting.stats.reset') }
  if (url === 'api/v1/preview/start') return { data: await requestCapability('peopleCounting.video.open') }
  if (url === 'api/v1/preview/stop') return { data: await requestCapability('peopleCounting.video.close') }
  throw Object.assign(new Error('MOCK_ROUTE_NOT_ALLOWED'), { code: 'MOCK_ROUTE_NOT_ALLOWED' })
}

const put = async () => { throw Object.assign(new Error('MOCK_ROUTE_NOT_ALLOWED'), { code: 'MOCK_ROUTE_NOT_ALLOWED' }) }
const del = async () => { throw Object.assign(new Error('MOCK_ROUTE_NOT_ALLOWED'), { code: 'MOCK_ROUTE_NOT_ALLOWED' }) }

export default { get, post, put, delete: del }
